#include <Databases/DataLake/DatabaseDataLake.h>

#if USE_AVRO
#include <Access/Common/HTTPAuthenticationScheme.h>
#include <Core/Settings.h>

#include <Databases/DatabaseFactory.h>
#include <Databases/DataLake/UnityCatalog.h>
#include <Databases/DataLake/RestCatalog.h>
#include <DataTypes/DataTypeString.h>

#include <Storages/ObjectStorage/S3/Configuration.h>
#include <Storages/ConstraintsDescription.h>
#include <Storages/StorageNull.h>
#include <Storages/ObjectStorage/DataLakes/DataLakeConfiguration.h>

#include <Interpreters/evaluateConstantExpression.h>
#include <Interpreters/Context.h>
#include <Interpreters/StorageID.h>

#include <Formats/FormatFactory.h>

#include <Parsers/ASTCreateQuery.h>
#include <Parsers/ASTLiteral.h>
#include <Parsers/ASTFunction.h>
#include <Parsers/ASTDataType.h>


namespace DB
{
namespace DatabaseDataLakeSetting
{
    extern const DatabaseDataLakeSettingsDatabaseDataLakeCatalogType catalog_type;
    extern const DatabaseDataLakeSettingsString warehouse;
    extern const DatabaseDataLakeSettingsString catalog_credential;
    extern const DatabaseDataLakeSettingsString auth_header;
    extern const DatabaseDataLakeSettingsString auth_scope;
    extern const DatabaseDataLakeSettingsString storage_endpoint;
    extern const DatabaseDataLakeSettingsString oauth_server_uri;
    extern const DatabaseDataLakeSettingsBool vended_credentials;
}
namespace Setting
{
    extern const SettingsBool allow_experimental_database_iceberg;
    extern const SettingsBool use_hive_partitioning;
}

namespace ErrorCodes
{
    extern const int BAD_ARGUMENTS;
    extern const int SUPPORT_IS_DISABLED;
}

namespace
{
    /// Parse a string, containing at least one dot, into a two substrings:
    /// A.B.C.D.E -> A.B.C.D and E, where
    /// `A.B.C.D` is a table "namespace".
    /// `E` is a table name.
    std::pair<std::string, std::string> parseTableName(const std::string & name)
    {
        auto pos = name.rfind('.');
        if (pos == std::string::npos)
            throw DB::Exception(ErrorCodes::BAD_ARGUMENTS, "Table cannot have empty namespace: {}", name);

        auto table_name = name.substr(pos + 1);
        auto namespace_name = name.substr(0, name.size() - table_name.size() - 1);
        return {namespace_name, table_name};
    }
}

DatabaseDataLake::DatabaseDataLake(
    const std::string & database_name_,
    const std::string & url_,
    const DatabaseDataLakeSettings & settings_,
    ASTPtr database_engine_definition_,
    ASTPtr table_engine_definition_)
    : IDatabase(database_name_)
    , url(url_)
    , settings(settings_)
    , database_engine_definition(database_engine_definition_)
    , table_engine_definition(table_engine_definition_)
    , log(getLogger("DatabaseDataLake(" + database_name_ + ")"))
{
    validateSettings();
}

void DatabaseDataLake::validateSettings()
{
    if (settings[DatabaseDataLakeSetting::warehouse].value.empty())
    {
        throw Exception(
            ErrorCodes::BAD_ARGUMENTS, "`warehouse` setting cannot be empty. "
            "Please specify 'SETTINGS warehouse=<warehouse_name>' in the CREATE DATABASE query");
    }
}

std::shared_ptr<DataLake::ICatalog> DatabaseDataLake::getCatalog() const
{
    if (catalog_impl)
        return catalog_impl;

    switch (settings[DatabaseDataLakeSetting::catalog_type].value)
    {
        case DB::DatabaseDataLakeCatalogType::ICEBERG_REST:
        {
            catalog_impl = std::make_shared<DataLake::RestCatalog>(
                settings[DatabaseDataLakeSetting::warehouse].value,
                url,
                settings[DatabaseDataLakeSetting::catalog_credential].value,
                settings[DatabaseDataLakeSetting::auth_scope].value,
                settings[DatabaseDataLakeSetting::auth_header],
                settings[DatabaseDataLakeSetting::oauth_server_uri].value,
                Context::getGlobalContextInstance());
            break;
        }
        case DB::DatabaseDataLakeCatalogType::UNITY:
        {
            catalog_impl = std::make_shared<DataLake::UnityCatalog>(
                settings[DatabaseDataLakeSetting::warehouse].value,
                url,
                settings[DatabaseDataLakeSetting::catalog_credential].value,
                Context::getGlobalContextInstance());
            break;
        }
        default:
        {
            throw Exception(ErrorCodes::BAD_ARGUMENTS, "Unknown catalog type specified {}", settings[DatabaseDataLakeSetting::catalog_type].value);
        }
    }
    return catalog_impl;
}

std::shared_ptr<StorageObjectStorage::Configuration> DatabaseDataLake::getConfiguration(DatabaseDataLakeStorageType type) const
{
    /// TODO: add tests for azure, local storage types.

    auto catalog = getCatalog();
    switch (catalog->getCatalogType())
    {
        case DatabaseDataLakeCatalogType::ICEBERG_REST:
        {
            switch (type)
            {
#if USE_AWS_S3
                case DB::DatabaseDataLakeStorageType::S3:
                {
                    return std::make_shared<StorageS3IcebergConfiguration>();
                }
#endif
#if USE_AZURE_BLOB_STORAGE
                case DB::DatabaseDataLakeStorageType::Azure:
                {
                    return std::make_shared<StorageAzureIcebergConfiguration>();
                }
#endif
#if USE_HDFS
                case DB::DatabaseDataLakeStorageType::HDFS:
                {
                    return std::make_shared<StorageHDFSIcebergConfiguration>();
                }
#endif
                case DB::DatabaseDataLakeStorageType::Local:
                {
                    return std::make_shared<StorageLocalIcebergConfiguration>();
                }
                case DB::DatabaseDataLakeStorageType::Other:
                {
                    return std::make_shared<StorageLocalIcebergConfiguration>();
                }
#if !USE_AWS_S3 || !USE_AZURE_BLOB_STORAGE || !USE_HDFS
                default:
                    throw Exception(ErrorCodes::BAD_ARGUMENTS,
                                    "Server does not contain support for storage type {}",
                                    type);
#endif
            }
        }
        case DatabaseDataLakeCatalogType::UNITY:
        {
            switch (type)
            {
#if USE_AWS_S3
                case DB::DatabaseDataLakeStorageType::S3:
                {
                    return std::make_shared<StorageS3DeltaLakeConfiguration>();
                }
#endif
                case DB::DatabaseDataLakeStorageType::Local:
                {
                    return std::make_shared<StorageLocalDeltaLakeConfiguration>();
                }
                case DB::DatabaseDataLakeStorageType::Other:
                {
                    return std::make_shared<StorageLocalDeltaLakeConfiguration>();
                }
                default:
                    throw Exception(ErrorCodes::BAD_ARGUMENTS,
                                    "Server does not contain support for storage type {}",
                                    type);
            }
        }
        default:
        {
            throw Exception(ErrorCodes::BAD_ARGUMENTS,
                            "Server does not contain support for catalog type {}",
                            catalog->getCatalogType());
        }
    }
}

std::string DatabaseDataLake::getStorageEndpointForTable(const DataLake::TableMetadata & table_metadata) const
{
    auto endpoint_from_settings = settings[DatabaseDataLakeSetting::storage_endpoint].value;
    if (endpoint_from_settings.empty())
        return table_metadata.getLocation();
    else
        return table_metadata.getLocationWithEndpoint(endpoint_from_settings);

}

bool DatabaseDataLake::empty() const
{
    return getCatalog()->empty();
}

bool DatabaseDataLake::isTableExist(const String & name, ContextPtr /* context_ */) const
{
    const auto [namespace_name, table_name] = parseTableName(name);
    return getCatalog()->existsTable(namespace_name, table_name);
}

StoragePtr DatabaseDataLake::tryGetTable(const String & name, ContextPtr context_) const
{
    return tryGetTableImpl(name, context_, false);
}

StoragePtr DatabaseDataLake::tryGetTableImpl(const String & name, ContextPtr context_, bool lightweight) const
{
    auto catalog = getCatalog();
    auto table_metadata = DataLake::TableMetadata().withSchema();
    if (!lightweight)
        table_metadata = table_metadata.withLocation();
    else
        table_metadata = table_metadata.withLocationIfExists();

    const bool with_vended_credentials = settings[DatabaseDataLakeSetting::vended_credentials].value;
    if (with_vended_credentials && !lightweight)
        table_metadata = table_metadata.withStorageCredentials();

    auto [namespace_name, table_name] = parseTableName(name);

    if (!catalog->tryGetTableMetadata(namespace_name, table_name, table_metadata))
        return nullptr;

    /// Take database engine definition AST as base.
    ASTStorage * storage = database_engine_definition->as<ASTStorage>();
    ASTs args = storage->engine->arguments->children;

    if (!lightweight || table_metadata.hasLocation())
    {
        /// Replace Iceberg Catalog endpoint with storage path endpoint of requested table.
        auto table_endpoint = getStorageEndpointForTable(table_metadata);
        LOG_DEBUG(log, "Table endpoint {}", table_endpoint);
        if (table_endpoint.starts_with("file:/"))
            table_endpoint = table_endpoint.substr(std::string_view{"file:/"}.length());
        args[0] = std::make_shared<ASTLiteral>(table_endpoint);
    }


    /// We either fetch storage credentials from catalog
    /// or get storage credentials from database engine arguments
    /// in CREATE query (e.g. in `args`).
    /// Vended credentials can be disabled in catalog itself,
    /// so we have a separate setting to know whether we should even try to fetch them.
    if (with_vended_credentials && args.size() == 1)
    {
        if (!lightweight)
        {
            LOG_DEBUG(log, "Getting credentials");
            auto storage_credentials = table_metadata.getStorageCredentials();
            if (storage_credentials)
                storage_credentials->addCredentialsToEngineArgs(args);
        }
    }
    else if (args.size() == 1)
    {
        throw Exception(
            ErrorCodes::BAD_ARGUMENTS,
            "Either vended credentials need to be enabled "
            "or storage credentials need to be specified in database engine arguments in CREATE query");
    }

    LOG_TEST(log, "Using table endpoint: {}", args[0]->as<ASTLiteral>()->value.safeGet<String>());

    const auto columns = ColumnsDescription(table_metadata.getSchema());

    DatabaseDataLakeStorageType storage_type = DatabaseDataLakeStorageType::Other;
    auto storage_type_from_catalog = catalog->getStorageType();
    if (storage_type_from_catalog.has_value())
    {
        storage_type = storage_type_from_catalog.value();
    }
    else
    {
        if (table_metadata.hasLocation() || !lightweight)
            storage_type = table_metadata.getStorageType();
    }

    const auto configuration = getConfiguration(storage_type);
    auto storage_settings = std::make_unique<StorageObjectStorageSettings>();

    /// HACK: Hacky-hack to enable lazy load
    ContextMutablePtr context_copy = Context::createCopy(context_);
    Settings settings_copy = context_copy->getSettingsCopy();
    settings_copy[Setting::use_hive_partitioning] = false;
    context_copy->setSettings(settings_copy);

    /// with_table_structure = false: because there will be
    /// no table structure in table definition AST.
    StorageObjectStorage::Configuration::initialize(*configuration, args, context_copy, /* with_table_structure */false, storage_settings.get());

    return std::make_shared<StorageObjectStorage>(
        configuration,
        configuration->createObjectStorage(context_copy, /* is_readonly */ false),
        context_copy,
        StorageID(getDatabaseName(), name),
        /* columns */columns,
        /* constraints */ConstraintsDescription{},
        /* comment */"",
        getFormatSettings(context_copy),
        LoadingStrictnessLevel::CREATE,
        /* distributed_processing */false,
        /* partition_by */nullptr,
        /* lazy_init */true);
}

DatabaseTablesIteratorPtr DatabaseDataLake::getTablesIterator(
    ContextPtr context_,
    const FilterByNameFunction & filter_by_table_name,
    bool /* skip_not_loaded */) const
{
    Tables tables;
    auto catalog = getCatalog();
    const auto iceberg_tables = catalog->getTables();

    for (const auto & table_name : iceberg_tables)
    {
        if (filter_by_table_name && !filter_by_table_name(table_name))
            continue;

        auto storage = tryGetTable(table_name, context_);
        [[maybe_unused]] bool inserted = tables.emplace(table_name, storage).second;
        chassert(inserted);
    }

    return std::make_unique<DatabaseTablesSnapshotIterator>(tables, getDatabaseName());
}

DatabaseTablesIteratorPtr DatabaseDataLake::getLightweightTablesIterator(
    ContextPtr context_,
    const FilterByNameFunction & filter_by_table_name,
    bool /*skip_not_loaded*/) const
{
     Tables tables;
    auto catalog = getCatalog();
    const auto iceberg_tables = catalog->getTables();

    for (const auto & table_name : iceberg_tables)
    {
        if (filter_by_table_name && !filter_by_table_name(table_name))
            continue;

        auto storage = tryGetTableImpl(table_name, context_, true);
        [[maybe_unused]] bool inserted = tables.emplace(table_name, storage).second;
        chassert(inserted);
    }

    return std::make_unique<DatabaseTablesSnapshotIterator>(tables, getDatabaseName());
}

ASTPtr DatabaseDataLake::getCreateDatabaseQuery() const
{
    const auto & create_query = std::make_shared<ASTCreateQuery>();
    create_query->setDatabase(getDatabaseName());
    create_query->set(create_query->storage, database_engine_definition);
    return create_query;
}

ASTPtr DatabaseDataLake::getCreateTableQueryImpl(
    const String & name,
    ContextPtr /* context_ */,
    bool /* throw_on_error */) const
{
    auto catalog = getCatalog();
    auto table_metadata = DataLake::TableMetadata().withLocation().withSchema();

    const auto [namespace_name, table_name] = parseTableName(name);
    catalog->getTableMetadata(namespace_name, table_name, table_metadata);

    auto create_table_query = std::make_shared<ASTCreateQuery>();
    auto table_storage_define = table_engine_definition->clone();

    auto * storage = table_storage_define->as<ASTStorage>();
    storage->engine->kind = ASTFunction::Kind::TABLE_ENGINE;
    if (!table_metadata.isDefaultReadableTable())
        storage->engine->name = "Other";

    storage->settings = {};

    create_table_query->set(create_table_query->storage, table_storage_define);

    auto columns_declare_list = std::make_shared<ASTColumns>();
    auto columns_expression_list = std::make_shared<ASTExpressionList>();

    columns_declare_list->set(columns_declare_list->columns, columns_expression_list);
    create_table_query->set(create_table_query->columns_list, columns_declare_list);

    create_table_query->setTable(name);
    create_table_query->setDatabase(getDatabaseName());

    for (const auto & column_type_and_name : table_metadata.getSchema())
    {
        LOG_DEBUG(log, "Processing column {}", column_type_and_name.name);
        const auto column_declaration = std::make_shared<ASTColumnDeclaration>();
        column_declaration->name = column_type_and_name.name;
        column_declaration->type = makeASTDataType(column_type_and_name.type->getName());
        columns_expression_list->children.emplace_back(column_declaration);
    }

    auto storage_engine_arguments = storage->engine->arguments;
    if (table_metadata.isDefaultReadableTable())
    {
        if (storage_engine_arguments->children.empty())
        {
            throw Exception(
                ErrorCodes::BAD_ARGUMENTS, "Unexpected number of arguments: {}",
                storage_engine_arguments->children.size());
        }
        auto table_endpoint = getStorageEndpointForTable(table_metadata);
        if (table_endpoint.starts_with("file:/"))
            table_endpoint = table_endpoint.substr(std::string_view{"file:/"}.length());

        LOG_DEBUG(log, "Table endpoint {}", table_endpoint);
        storage_engine_arguments->children[0] = std::make_shared<ASTLiteral>(table_endpoint);
    }
    else
    {
        storage_engine_arguments->children.clear();
    }

    return create_table_query;
}

void registerDatabaseDataLake(DatabaseFactory & factory)
{
    auto create_fn = [](const DatabaseFactory::Arguments & args)
    {
        if (!args.create_query.attach
            && !args.context->getSettingsRef()[Setting::allow_experimental_database_iceberg])
        {
            throw Exception(ErrorCodes::SUPPORT_IS_DISABLED,
                            "DatabaseDataLake engine is experimental. "
                            "To allow its usage, enable setting allow_experimental_database_iceberg");
        }

        const auto * database_engine_define = args.create_query.storage;
        const auto & database_engine_name = args.engine_name;

        const ASTFunction * function_define = database_engine_define->engine;
        if (!function_define->arguments)
            throw Exception(ErrorCodes::BAD_ARGUMENTS, "Engine `{}` must have arguments", database_engine_name);

        ASTs & engine_args = function_define->arguments->children;
        if (engine_args.empty())
            throw Exception(ErrorCodes::BAD_ARGUMENTS, "Engine `{}` must have arguments", database_engine_name);

        for (auto & engine_arg : engine_args)
            engine_arg = evaluateConstantExpressionOrIdentifierAsLiteral(engine_arg, args.context);

        const auto url = engine_args[0]->as<ASTLiteral>()->value.safeGet<String>();

        DatabaseDataLakeSettings database_settings;
        if (database_engine_define->settings)
            database_settings.loadFromQuery(*database_engine_define);

        if (database_engine_name == "IcebergRestCatalog")
        {
            database_settings[DB::DatabaseDataLakeSetting::catalog_type] = DB::DatabaseDataLakeCatalogType::ICEBERG_REST;
        }
        else if (database_engine_name == "UnityCatalog")
        {
            database_settings[DB::DatabaseDataLakeSetting::catalog_type] = DB::DatabaseDataLakeCatalogType::UNITY;
        }
        else if (database_engine_name == "DataLakeCatalog")
        {
            if (database_settings[DB::DatabaseDataLakeSetting::catalog_type] == DB::DatabaseDataLakeCatalogType::UNKNOWN)
                throw Exception(
                    ErrorCodes::BAD_ARGUMENTS,
                    "If generic database engine is specified (`{}`), the catalog implementation must be speicified in `SETTINGS catalog_type = 'XXX'`",
                    database_engine_name);
        }
        else
        {
            throw Exception(ErrorCodes::LOGICAL_ERROR, "Unknown engine name {}", database_engine_name);
        }

        auto engine_for_tables = database_engine_define->clone();
        ASTFunction * engine_func = engine_for_tables->as<ASTStorage &>().engine;

        LOG_DEBUG(&Poco::Logger::get("DEBUG"), "Database engine name {}", database_engine_name);

        switch (database_settings[DB::DatabaseDataLakeSetting::catalog_type].value)
        {
            case DatabaseDataLakeCatalogType::ICEBERG_REST:
            {
                engine_func->name = "Iceberg";
                break;
            }
            case DatabaseDataLakeCatalogType::UNITY:
            {
                engine_func->name = "DeltaLake";
                break;
            }
            default:
            {
                throw Exception(ErrorCodes::LOGICAL_ERROR, "Unknown engine name {}", database_engine_name);
            }
        }

        LOG_DEBUG(&Poco::Logger::get("DEBUG"), "Database engine name {}", database_engine_name);
        return std::make_shared<DatabaseDataLake>(
            args.database_name,
            url,
            database_settings,
            database_engine_define->clone(),
            std::move(engine_for_tables));
    };
    factory.registerDatabase("UnityCatalog", create_fn, { .supports_arguments = true, .supports_settings = true });
    factory.registerDatabase("IcebergRestCatalog", create_fn, { .supports_arguments = true, .supports_settings = true });
    factory.registerDatabase("DataLakeCatalog", create_fn, { .supports_arguments = true, .supports_settings = true });
}

}

#endif
