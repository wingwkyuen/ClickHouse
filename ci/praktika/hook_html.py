import dataclasses
import json
from pathlib import Path
from typing import List

from praktika.utils import Shell

from ._environment import _Environment
from .gh import GH
from .info import Info
from .parser import WorkflowConfigParser
from .result import Result, ResultInfo, _ResultS3
from .runtime import RunConfig
from .s3 import S3
from .settings import Settings
from .utils import Utils


@dataclasses.dataclass
class GitCommit:
    # date: str
    # message: str
    sha: str

    @staticmethod
    def from_json(file) -> List["GitCommit"]:
        commits = []
        json_data = None
        try:
            with open(file, "r", encoding="utf-8") as f:
                json_data = json.load(f)
            commits = [
                GitCommit(
                    # message=commit["messageHeadline"],
                    sha=commit["sha"],
                    # date=commit["committedDate"],
                )
                for commit in json_data
            ]
        except Exception as e:
            print(
                f"ERROR: Failed to deserialize commit's data [{json_data}], ex: [{e}]"
            )

        return commits

    @classmethod
    def update_s3_data(cls):
        env = _Environment.get()
        sha = env.SHA
        if not sha:
            print("WARNING: Failed to retrieve commit sha")
            return
        commits = cls.pull_from_s3()
        for commit in commits:
            if sha == commit.sha:
                print(
                    f"INFO: Sha already present in commits data [{sha}] - skip data update"
                )
                return
        commits.append(GitCommit(sha=sha))
        commits = commits[
            -20:
        ]  # limit maximum number of commits from the past to show in the report
        cls.push_to_s3(commits)
        return

    @classmethod
    def dump(cls, commits):
        commits_ = []
        for commit in commits:
            commits_.append(dataclasses.asdict(commit))
        with open(cls.file_name(), "w", encoding="utf8") as f:
            json.dump(commits_, f)

    @classmethod
    def get_s3_path(cls):
        env = _Environment.get()
        if env.PR_NUMBER:
            s3suffix = f"PRs/{env.PR_NUMBER}"
        else:
            assert env.BRANCH
            s3suffix = f"REFs/{env.BRANCH}"
        return f"{Settings.HTML_S3_PATH}/{s3suffix}"

    @classmethod
    def pull_from_s3(cls):
        local_path = Path(cls.file_name())
        file_name = local_path.name
        s3_path = f"{cls.get_s3_path()}/{file_name}"
        if not S3.copy_file_from_s3(s3_path=s3_path, local_path=local_path):
            print(f"WARNING: failed to cp file [{s3_path}] from s3")
            return []
        return cls.from_json(local_path)

    @classmethod
    def push_to_s3(cls, commits):
        print(f"INFO: push commits data to s3, commits num [{len(commits)}]")
        cls.dump(commits)
        local_path = Path(cls.file_name())
        file_name = local_path.name
        s3_path = f"{cls.get_s3_path()}/{file_name}"
        if not S3.copy_file_to_s3(s3_path=s3_path, local_path=local_path, text=True):
            print(f"WARNING: failed to cp file [{local_path}] to s3")

    @classmethod
    def file_name(cls):
        return f"{Settings.TEMP_DIR}/commits.json"

    # def _get_pr_commits(pr_number):
    #     res = []
    #     if not pr_number:
    #         return res
    #     output = Shell.get_output(f"gh pr view {pr_number}  --json commits")
    #     if output:
    #         res = GitCommit.from_json(output)
    #     return res


class HtmlRunnerHooks:
    @classmethod
    def push_pending_ci_report(cls, _workflow):
        # generate pending Results for all jobs in the workflow
        env = _Environment.get()
        results = []
        for job in _workflow.jobs:
            if job.name == Settings.CI_CONFIG_JOB_NAME:
                # fetch running status with start_time for current job
                result = Result.from_fs(job.name)
            else:
                result = Result.generate_pending(job.name)
            results.append(result)
        summary_result = Result.generate_pending(_workflow.name, results=results)
        summary_result.start_time = Utils.timestamp()
        summary_result.links.append(env.CHANGE_URL)
        summary_result.links.append(env.RUN_URL)
        summary_result.start_time = Utils.timestamp()
        info = Info()
        summary_result.set_info(
            f"{info.pr_title}  |  {info.git_branch}  |  {info.git_sha}"
            if info.pr_number
            else f"{info.git_branch}  |  {info.git_sha}"
        )
        assert _ResultS3.copy_result_to_s3_with_version(summary_result, version=0)
        page_url = Info().get_report_url(latest=True)
        print(f"CI Status page url [{page_url}]")

        if Settings.USE_CUSTOM_GH_AUTH:
            from praktika.gh_auth_deprecated import GHAuth

            pem = _workflow.get_secret(Settings.SECRET_GH_APP_PEM_KEY).get_value()
            app_id = _workflow.get_secret(Settings.SECRET_GH_APP_ID).get_value()
            GHAuth.auth(app_key=pem, app_id=app_id)

        res2 = not bool(env.PR_NUMBER) or GH.post_pr_comment(
            comment_body=f"Workflow [[{_workflow.name}]({page_url})], commit [{_Environment.get().SHA[:8]}]",
            or_update_comment_with_substring=f"Workflow [[{_workflow.name}]",
        )
        res1 = GH.post_commit_status(
            name=_workflow.name,
            status=Result.Status.PENDING,
            description="",
            url=page_url,
        )
        if not (res1 or res2):
            Utils.raise_with_error(
                "Failed to set both GH commit status and PR comment with Workflow Status, cannot proceed"
            )
        GitCommit.update_s3_data()

    @classmethod
    def configure(cls, _workflow):
        # generate pending Results for all jobs in the workflow
        if _workflow.enable_cache:
            skip_jobs = RunConfig.from_fs(_workflow.name).cache_success
            job_cache_records = RunConfig.from_fs(_workflow.name).cache_jobs
            results = []
            for job in _workflow.jobs:
                if job.name in skip_jobs:
                    result = Result.generate_skipped(
                        job.name, job_cache_records[job.name]
                    )
                    results.append(result)
            if results:
                assert _ResultS3.update_workflow_results(
                    _workflow.name, new_sub_results=results
                )

    @classmethod
    def pre_run(cls, _workflow, _job):
        result = Result.from_fs(_job.name)
        _ResultS3.update_workflow_results(
            workflow_name=_workflow.name, new_sub_results=result
        )

    @classmethod
    def run(cls, _workflow, _job):
        pass

    @classmethod
    def post_run(cls, _workflow, _job, info_errors):
        result = Result.from_fs(_job.name)
        _ResultS3.upload_result_files_to_s3(result).dump()
        _ResultS3.copy_result_to_s3(result)

        env = _Environment.get()

        new_sub_results = [result]
        new_result_info = ""
        env_info = env.REPORT_INFO
        if env_info:
            print(
                f"WARNING: some info lines are set in Environment - append to report [{env_info}]"
            )
            info_errors += env_info
        if info_errors:
            info_errors = [f"    |  {error}" for error in info_errors]
            info_str = f"{_job.name}:\n"
            info_str += "\n".join(info_errors)
            print("Update workflow results with new info")
            new_result_info = info_str

        if not result.is_ok():
            print(
                "Current job failed - find dependee jobs in the workflow and set their statuses to skipped"
            )
            workflow_config_parsed = WorkflowConfigParser(_workflow).parse()

            dependees = set()

            def add_dependees(job_name):
                for dependee_job in workflow_config_parsed.workflow_yaml_config.jobs:
                    if dependee_job.run_unless_cancelled:
                        continue
                    if (
                        job_name in dependee_job.needs
                        and dependee_job.name not in dependees
                    ):
                        dependees.add(dependee_job.name)
                        add_dependees(dependee_job.name)

            add_dependees(_job.name)

            for dependee in dependees:
                print(
                    f"NOTE: Set job [{dependee}] status to [{Result.Status.SKIPPED}] due to current failure"
                )
                new_sub_results.append(
                    Result(
                        name=dependee,
                        status=Result.Status.SKIPPED,
                        info=ResultInfo.SKIPPED_DUE_TO_PREVIOUS_FAILURE
                        + f" [{_job.name}]",
                    )
                )

        updated_status = _ResultS3.update_workflow_results(
            new_info=new_result_info,
            new_sub_results=new_sub_results,
            workflow_name=_workflow.name,
        )

        if updated_status:
            if Settings.USE_CUSTOM_GH_AUTH:
                from praktika.gh_auth_deprecated import GHAuth

                pem = _workflow.get_secret(Settings.SECRET_GH_APP_PEM_KEY).get_value()
                app_id = _workflow.get_secret(Settings.SECRET_GH_APP_ID).get_value()
                GHAuth.auth(app_key=pem, app_id=app_id)

            print(f"Update GH commit status [{result.name}]: [{updated_status}]")
            GH.post_commit_status(
                name=_workflow.name,
                status=GH.convert_to_gh_status(updated_status),
                description="",
                url=Info().get_report_url(latest=True),
            )
