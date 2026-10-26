// The schemas of the configuration database and the product database at
// their latest Alembic revisions, per dialect (architecture.md §11.5,
// server-database.md §4). They create the same tables, columns, constraints,
// indexes and initial rows as the Python version's migrations do. Included
// by schema.cc only.

// The configuration database at revision 635389f535cd, SQLite.
constexpr std::string_view kConfigSchemaSqlite = R"sql(
CREATE TABLE alembic_version (
  version_num VARCHAR(32) NOT NULL,
  CONSTRAINT alembic_version_pkc PRIMARY KEY (version_num)
);
CREATE TABLE auth_sessions (
  id INTEGER NOT NULL,
  user_name VARCHAR,
  token CHAR(32) NOT NULL,
  groups VARCHAR,
  last_access DATETIME NOT NULL,
  description VARCHAR,
  "can_expire_UNUSED" BOOLEAN DEFAULT 1,
  CONSTRAINT pk_auth_sessions PRIMARY KEY (id),
  CONSTRAINT uq_auth_sessions_token UNIQUE (token)
);
CREATE TABLE products (
  id INTEGER NOT NULL,
  endpoint VARCHAR NOT NULL,
  connection VARCHAR NOT NULL,
  display_name VARCHAR NOT NULL,
  description TEXT,
  run_limit INTEGER,
  is_review_status_change_disabled BOOLEAN DEFAULT 0,
  latest_storage_date DATETIME,
  num_of_runs INTEGER DEFAULT '0',
  confidentiality VARCHAR,
  report_limit INTEGER DEFAULT '500000' NOT NULL,
  CONSTRAINT pk_products PRIMARY KEY (id),
  CONSTRAINT uq_products_endpoint UNIQUE (endpoint)
);
CREATE TABLE server_configurations (
  id INTEGER NOT NULL,
  config_key VARCHAR NOT NULL,
  config_value VARCHAR,
  CONSTRAINT pk_server_configurations PRIMARY KEY (id)
);
CREATE TABLE permissions_product (
  id INTEGER NOT NULL,
  permission VARCHAR(14),
  product_id INTEGER NOT NULL,
  name VARCHAR NOT NULL,
  is_group BOOLEAN NOT NULL,
  CONSTRAINT pk_permissions_product PRIMARY KEY (id),
  CONSTRAINT fk_permissions_product_product_id_products
    FOREIGN KEY(product_id) REFERENCES products (id)
    ON DELETE CASCADE NOT DEFERRABLE INITIALLY IMMEDIATE
);
CREATE TABLE permissions_system (
  id INTEGER NOT NULL,
  permission VARCHAR(15),
  name VARCHAR NOT NULL,
  is_group BOOLEAN NOT NULL,
  CONSTRAINT pk_permissions_system PRIMARY KEY (id)
);
CREATE TABLE oauth_sessions (
  id INTEGER NOT NULL,
  provider VARCHAR NOT NULL,
  state VARCHAR NOT NULL,
  code_verifier VARCHAR NOT NULL,
  expires_at DATETIME,
  CONSTRAINT pk_oauth_sessions PRIMARY KEY (id)
);
CREATE TABLE oauth_tokens (
  id INTEGER NOT NULL,
  access_token VARCHAR NOT NULL,
  expires_at DATETIME,
  refresh_token VARCHAR NOT NULL,
  auth_session_id INTEGER NOT NULL,
  CONSTRAINT pk_oauth_tokens PRIMARY KEY (id),
  CONSTRAINT fk_oauth_tokens_auth_session_id_auth_sessions
    FOREIGN KEY(auth_session_id) REFERENCES auth_sessions (id)
    ON DELETE CASCADE
);
CREATE TABLE background_tasks (
  machine_id VARCHAR,
  token CHAR(64) NOT NULL,
  kind VARCHAR NOT NULL,
  status VARCHAR(9) NOT NULL,
  product_id INTEGER,
  summary VARCHAR NOT NULL,
  comments TEXT,
  username VARCHAR,
  enqueued_at DATETIME,
  started_at DATETIME,
  finished_at DATETIME,
  last_seen_at DATETIME,
  consumed BOOLEAN DEFAULT 0 NOT NULL,
  cancel_flag BOOLEAN DEFAULT 0 NOT NULL,
  CONSTRAINT pk_background_tasks PRIMARY KEY (token),
  CONSTRAINT fk_background_tasks_product_id_products
    FOREIGN KEY(product_id) REFERENCES products (id)
    ON DELETE CASCADE NOT DEFERRABLE INITIALLY IMMEDIATE
);
CREATE INDEX ix_background_tasks_kind ON background_tasks (kind);
CREATE INDEX ix_background_tasks_machine_id ON background_tasks (machine_id);
CREATE INDEX ix_background_tasks_product_id ON background_tasks (product_id);
CREATE INDEX ix_background_tasks_status ON background_tasks (status);
CREATE TABLE personal_access_tokens (
  id INTEGER NOT NULL,
  user_name VARCHAR,
  token_name VARCHAR,
  token CHAR(32) NOT NULL,
  description VARCHAR,
  groups VARCHAR,
  last_access DATETIME NOT NULL,
  expiration DATETIME,
  creation_date DATETIME NOT NULL,
  CONSTRAINT pk_personal_access_tokens PRIMARY KEY (id),
  CONSTRAINT uq_personal_access_tokens_user_name UNIQUE (user_name, token_name),
  CONSTRAINT uq_personal_access_tokens_token UNIQUE (token)
);
CREATE INDEX ix_oauth_tokens_auth_session_id ON oauth_tokens (auth_session_id);
INSERT INTO alembic_version (version_num) VALUES ('635389f535cd');
)sql";

// The configuration database at revision 635389f535cd, PostgreSQL.
constexpr std::string_view kConfigSchemaPostgresql = R"sql(
CREATE TYPE background_task_statuses AS ENUM (
  'allocated', 'enqueued', 'running', 'completed', 'failed', 'cancelled',
  'dropped');
CREATE TYPE product_perms AS ENUM (
  'PRODUCT_ADMIN', 'PRODUCT_STORE', 'PRODUCT_ACCESS', 'PRODUCT_VIEW');
CREATE TYPE sys_perms AS ENUM ('SUPERUSER', 'PERMISSION_VIEW');
CREATE TABLE alembic_version (
  version_num character varying(32) NOT NULL,
  CONSTRAINT alembic_version_pkc PRIMARY KEY (version_num)
);
CREATE TABLE auth_sessions (
  id serial NOT NULL,
  user_name character varying,
  token character(32) NOT NULL,
  groups character varying,
  last_access timestamp without time zone NOT NULL,
  description character varying,
  CONSTRAINT pk_auth_sessions PRIMARY KEY (id),
  CONSTRAINT uq_auth_sessions_token UNIQUE (token)
);
CREATE TABLE products (
  id serial NOT NULL,
  endpoint character varying NOT NULL,
  connection character varying NOT NULL,
  display_name character varying NOT NULL,
  description text,
  run_limit integer,
  is_review_status_change_disabled boolean DEFAULT false,
  latest_storage_date timestamp without time zone,
  num_of_runs integer DEFAULT 0,
  confidentiality character varying,
  report_limit integer DEFAULT 500000 NOT NULL,
  CONSTRAINT pk_products PRIMARY KEY (id),
  CONSTRAINT uq_products_endpoint UNIQUE (endpoint)
);
CREATE TABLE server_configurations (
  id serial NOT NULL,
  config_key character varying NOT NULL,
  config_value character varying,
  CONSTRAINT pk_server_configurations PRIMARY KEY (id)
);
CREATE TABLE permissions_product (
  id serial NOT NULL,
  permission product_perms,
  product_id integer NOT NULL,
  name character varying NOT NULL,
  is_group boolean NOT NULL,
  CONSTRAINT pk_permissions_product PRIMARY KEY (id),
  CONSTRAINT fk_permissions_product_product_id_products
    FOREIGN KEY (product_id) REFERENCES products(id) ON DELETE CASCADE
);
CREATE TABLE permissions_system (
  id serial NOT NULL,
  permission sys_perms,
  name character varying NOT NULL,
  is_group boolean NOT NULL,
  CONSTRAINT pk_permissions_system PRIMARY KEY (id)
);
CREATE TABLE oauth_sessions (
  id serial NOT NULL,
  provider character varying NOT NULL,
  state character varying NOT NULL,
  code_verifier character varying NOT NULL,
  expires_at timestamp without time zone,
  CONSTRAINT pk_oauth_sessions PRIMARY KEY (id)
);
CREATE TABLE oauth_tokens (
  id serial NOT NULL,
  access_token character varying NOT NULL,
  expires_at timestamp without time zone,
  refresh_token character varying NOT NULL,
  auth_session_id integer NOT NULL,
  CONSTRAINT pk_oauth_tokens PRIMARY KEY (id),
  CONSTRAINT fk_oauth_tokens_auth_session_id_auth_sessions
    FOREIGN KEY (auth_session_id) REFERENCES auth_sessions(id)
    ON DELETE CASCADE
);
CREATE TABLE background_tasks (
  machine_id character varying,
  token character(64) NOT NULL,
  kind character varying NOT NULL,
  status background_task_statuses NOT NULL,
  product_id integer,
  summary character varying NOT NULL,
  comments text,
  username character varying,
  enqueued_at timestamp without time zone,
  started_at timestamp without time zone,
  finished_at timestamp without time zone,
  last_seen_at timestamp without time zone,
  consumed boolean DEFAULT false NOT NULL,
  cancel_flag boolean DEFAULT false NOT NULL,
  CONSTRAINT pk_background_tasks PRIMARY KEY (token),
  CONSTRAINT fk_background_tasks_product_id_products
    FOREIGN KEY (product_id) REFERENCES products(id) ON DELETE CASCADE
);
CREATE INDEX ix_background_tasks_kind ON background_tasks USING btree (kind);
CREATE INDEX ix_background_tasks_machine_id
  ON background_tasks USING btree (machine_id);
CREATE INDEX ix_background_tasks_product_id
  ON background_tasks USING btree (product_id);
CREATE INDEX ix_background_tasks_status
  ON background_tasks USING btree (status);
CREATE TABLE personal_access_tokens (
  id serial NOT NULL,
  user_name character varying,
  token_name character varying,
  token character(32) NOT NULL,
  description character varying,
  groups character varying,
  last_access timestamp without time zone NOT NULL,
  expiration timestamp without time zone,
  creation_date timestamp without time zone NOT NULL,
  CONSTRAINT pk_personal_access_tokens PRIMARY KEY (id),
  CONSTRAINT uq_personal_access_tokens_token UNIQUE (token),
  CONSTRAINT uq_personal_access_tokens_user_name UNIQUE (user_name, token_name)
);
CREATE INDEX ix_oauth_tokens_auth_session_id
  ON oauth_tokens USING btree (auth_session_id);
INSERT INTO alembic_version (version_num) VALUES ('635389f535cd');
)sql";

// The product database at revision feb5f01b5b52, SQLite. SQLite cannot drop
// columns, so the legacy columns the migrations renamed are still there.
constexpr std::string_view kProductSchemaSqlite = R"sql(
CREATE TABLE alembic_version (
  version_num VARCHAR(32) NOT NULL,
  CONSTRAINT alembic_version_pkc PRIMARY KEY (version_num)
);
CREATE TABLE comments (
  id INTEGER NOT NULL,
  bug_hash VARCHAR NOT NULL,
  author VARCHAR NOT NULL,
  message BLOB NOT NULL,
  created_at DATETIME NOT NULL,
  kind INTEGER DEFAULT '0' NOT NULL,
  CONSTRAINT pk_comments PRIMARY KEY (id)
);
CREATE INDEX ix_comments_bug_hash ON comments (bug_hash);
CREATE TABLE file_contents (
  content_hash VARCHAR NOT NULL,
  content BLOB,
  blame_info BLOB,
  CONSTRAINT pk_file_contents PRIMARY KEY (content_hash)
);
CREATE TABLE review_statuses (
  bug_hash VARCHAR NOT NULL,
  status VARCHAR(14) NOT NULL,
  author VARCHAR NOT NULL,
  message BLOB NOT NULL,
  date DATETIME NOT NULL,
  CONSTRAINT pk_review_statuses PRIMARY KEY (bug_hash)
);
CREATE TABLE runs (
  id INTEGER NOT NULL,
  date DATETIME,
  duration INTEGER,
  name VARCHAR,
  version VARCHAR,
  command VARCHAR,
  can_delete BOOLEAN DEFAULT 1 NOT NULL,
  CONSTRAINT pk_runs PRIMARY KEY (id),
  CONSTRAINT uq_runs_name UNIQUE (name)
);
CREATE TABLE files (
  id INTEGER NOT NULL,
  filepath VARCHAR NOT NULL,
  filename VARCHAR NOT NULL,
  content_hash VARCHAR,
  remote_url VARCHAR,
  tracking_branch VARCHAR,
  CONSTRAINT pk_files PRIMARY KEY (id),
  CONSTRAINT fk_files_content_hash_file_contents
    FOREIGN KEY(content_hash) REFERENCES file_contents (content_hash)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,
  CONSTRAINT uq_files_filepath UNIQUE (filepath, content_hash)
);
CREATE TABLE run_histories (
  id INTEGER NOT NULL,
  run_id INTEGER,
  version_tag VARCHAR,
  user VARCHAR NOT NULL,
  time DATETIME NOT NULL,
  check_command BLOB,
  cc_version VARCHAR,
  description VARCHAR,
  CONSTRAINT pk_run_histories PRIMARY KEY (id),
  CONSTRAINT fk_run_histories_run_id_runs
    FOREIGN KEY(run_id) REFERENCES runs (id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,
  CONSTRAINT uq_run_histories_run_id UNIQUE (run_id, version_tag)
);
CREATE INDEX ix_run_histories_run_id ON run_histories (run_id);
CREATE TABLE run_locks (
  name VARCHAR NOT NULL,
  locked_at DATETIME NOT NULL,
  username VARCHAR,
  CONSTRAINT pk_run_locks PRIMARY KEY (name)
);
CREATE TABLE source_components (
  name VARCHAR NOT NULL,
  value BLOB NOT NULL,
  description TEXT,
  username VARCHAR,
  CONSTRAINT pk_source_components PRIMARY KEY (name)
);
CREATE TABLE analyzer_statistics (
  id INTEGER NOT NULL,
  run_history_id INTEGER,
  analyzer_type VARCHAR,
  version BLOB,
  successful INTEGER,
  failed INTEGER,
  failed_files BLOB,
  CONSTRAINT pk_analyzer_statistics PRIMARY KEY (id),
  CONSTRAINT fk_analyzer_statistics_run_history_id_run_histories
    FOREIGN KEY(run_history_id) REFERENCES run_histories (id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED
);
CREATE INDEX ix_analyzer_statistics_run_history_id
  ON analyzer_statistics (run_history_id);
CREATE INDEX ix_files_content_hash ON files (content_hash);
CREATE TABLE analysis_info (
  id INTEGER NOT NULL,
  analyzer_command BLOB,
  checker_set_id INTEGER,
  CONSTRAINT pk_analysis_info PRIMARY KEY (id)
);
CREATE TABLE cleanup_plans (
  id INTEGER NOT NULL,
  name VARCHAR NOT NULL,
  due_date DATETIME,
  description VARCHAR,
  closed_at DATETIME,
  CONSTRAINT pk_cleanup_plans PRIMARY KEY (id),
  CONSTRAINT uq_cleanup_plans_name UNIQUE (name)
);
CREATE TABLE cleanup_plan_report_hashes (
  cleanup_plan_id INTEGER,
  bug_hash VARCHAR NOT NULL,
  CONSTRAINT pk_cleanup_plan_report_hashes PRIMARY KEY (bug_hash),
  CONSTRAINT fk_cleanup_plan_report_hashes_cleanup_plan_id_cleanup_plans
    FOREIGN KEY(cleanup_plan_id) REFERENCES cleanup_plans (id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED
);
CREATE INDEX ix_cleanup_plan_report_hashes_cleanup_plan_id
  ON cleanup_plan_report_hashes (cleanup_plan_id);
CREATE TABLE reports (
  id INTEGER NOT NULL,
  file_id INTEGER,
  run_id INTEGER,
  bug_id VARCHAR,
  "checker_name_MOVED_TO_checkers" VARCHAR,
  "checker_cat_UNUSED" VARCHAR,
  "bug_type_UNUSED" VARCHAR,
  "severity_MOVED_TO_checkers" INTEGER,
  line INTEGER,
  "column" INTEGER,
  checker_message VARCHAR,
  detection_status VARCHAR(11),
  detected_at DATETIME NOT NULL,
  fixed_at DATETIME,
  path_length INTEGER,
  "analyzer_name_MOVED_TO_checkers" VARCHAR DEFAULT 'unknown' NOT NULL,
  review_status VARCHAR(14) DEFAULT 'unreviewed',
  review_status_author VARCHAR,
  review_status_date DATETIME,
  review_status_is_in_source BOOLEAN DEFAULT 0,
  review_status_message BLOB,
  checker_id INTEGER DEFAULT '1' NOT NULL,
  CONSTRAINT pk_reports PRIMARY KEY (id),
  CONSTRAINT fk_reports_file_id_files
    FOREIGN KEY(file_id) REFERENCES files (id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,
  CONSTRAINT fk_reports_run_id_runs
    FOREIGN KEY(run_id) REFERENCES runs (id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED
);
CREATE INDEX ix_reports_bug_id ON reports (bug_id);
CREATE INDEX ix_reports_run_id ON reports (run_id);
CREATE INDEX ix_reports_file_id ON reports (file_id);
CREATE TABLE report_annotations (
  report_id INTEGER NOT NULL,
  "key" VARCHAR NOT NULL,
  value VARCHAR NOT NULL,
  CONSTRAINT pk_report_annotations PRIMARY KEY (report_id, "key"),
  CONSTRAINT fk_report_annotations_report_id_reports
    FOREIGN KEY(report_id) REFERENCES reports (id) ON DELETE CASCADE
);
CREATE INDEX ix_report_annotations_report_id
  ON report_annotations (report_id);
CREATE TABLE checkers (
  id INTEGER NOT NULL,
  analyzer_name VARCHAR,
  checker_name VARCHAR,
  severity INTEGER,
  CONSTRAINT pk_checkers PRIMARY KEY (id),
  CONSTRAINT uq_checkers_analyzer_name UNIQUE (analyzer_name, checker_name)
);
CREATE INDEX ix_checkers_severity ON checkers (severity);
CREATE INDEX ix_reports_checker_id ON reports (checker_id);
CREATE TABLE source_component_files (
  source_component_name VARCHAR NOT NULL,
  file_id INTEGER NOT NULL,
  CONSTRAINT pk_source_component_files
    PRIMARY KEY (source_component_name, file_id),
  CONSTRAINT fk_source_component_files_file_id_files
    FOREIGN KEY(file_id) REFERENCES files (id) ON DELETE CASCADE,
  CONSTRAINT fk_source_component_files_source_component_name_source_components
    FOREIGN KEY(source_component_name) REFERENCES source_components (name)
    ON DELETE CASCADE
);
CREATE TABLE filter_presets (
  id INTEGER NOT NULL,
  preset_name VARCHAR(100) NOT NULL,
  report_filter JSON NOT NULL,
  CONSTRAINT pk_filter_presets PRIMARY KEY (id),
  CONSTRAINT uq_filter_presets_preset_name UNIQUE (preset_name)
);
CREATE TABLE report_analysis_info (
  report_id INTEGER NOT NULL,
  analysis_info_id INTEGER NOT NULL,
  CONSTRAINT pk_report_analysis_info
    PRIMARY KEY (report_id, analysis_info_id),
  CONSTRAINT fk_report_analysis_info_report_id_reports
    FOREIGN KEY(report_id) REFERENCES reports (id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,
  CONSTRAINT fk_report_analysis_info_analysis_info_id_analysis_info
    FOREIGN KEY(analysis_info_id) REFERENCES analysis_info (id)
);
CREATE INDEX ix_report_analysis_info_report_id
  ON report_analysis_info (report_id);
CREATE TABLE run_history_analysis_info (
  run_history_id INTEGER NOT NULL,
  analysis_info_id INTEGER NOT NULL,
  CONSTRAINT pk_run_history_analysis_info
    PRIMARY KEY (run_history_id, analysis_info_id),
  CONSTRAINT fk_run_history_analysis_info_analysis_info_id_analysis_info
    FOREIGN KEY(analysis_info_id) REFERENCES analysis_info (id),
  CONSTRAINT fk_run_history_analysis_info_run_history_id_run_histories
    FOREIGN KEY(run_history_id) REFERENCES run_histories (id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED
);
CREATE INDEX ix_run_history_analysis_info_run_history_id
  ON run_history_analysis_info (run_history_id);
CREATE TABLE checker_set (
  id INTEGER NOT NULL,
  hash_digest VARCHAR NOT NULL,
  CONSTRAINT pk_checker_set PRIMARY KEY (id),
  CONSTRAINT uq_checker_set_hash_digest UNIQUE (hash_digest)
);
CREATE TABLE checker_set_items (
  checker_set_id INTEGER NOT NULL,
  checker_id INTEGER NOT NULL,
  enabled BOOLEAN NOT NULL,
  CONSTRAINT pk_checker_set_items PRIMARY KEY (checker_set_id, checker_id),
  CONSTRAINT fk_checker_set_items_checker_id_checkers
    FOREIGN KEY(checker_id) REFERENCES checkers (id)
    ON DELETE RESTRICT DEFERRABLE INITIALLY DEFERRED,
  CONSTRAINT fk_checker_set_items_checker_set_id_checker_set
    FOREIGN KEY(checker_set_id) REFERENCES checker_set (id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED
);
CREATE TABLE report_path_data (
  report_id BIGINT NOT NULL,
  path_data BLOB NOT NULL,
  CONSTRAINT pk_report_path_data PRIMARY KEY (report_id),
  CONSTRAINT fk_report_path_data_report_id_reports
    FOREIGN KEY(report_id) REFERENCES reports (id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED
);
CREATE TABLE report_path_data_files (
  report_path_data_id BIGINT NOT NULL,
  file_id INTEGER NOT NULL,
  CONSTRAINT pk_report_path_data_files
    PRIMARY KEY (report_path_data_id, file_id),
  CONSTRAINT fk_report_path_data_files_file_id_files
    FOREIGN KEY(file_id) REFERENCES files (id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,
  CONSTRAINT fk_report_path_data_files_report_path_data_id_report_path_data
    FOREIGN KEY(report_path_data_id) REFERENCES report_path_data (report_id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED
);
CREATE INDEX ix_report_path_data_files_file_id
  ON report_path_data_files (file_id);
CREATE INDEX ix_report_path_data_files_report_path_data_id
  ON report_path_data_files (report_path_data_id);
CREATE TABLE analysis_info_files (
  analysis_info_id INTEGER NOT NULL,
  filename VARCHAR NOT NULL,
  content_hash VARCHAR NOT NULL,
  CONSTRAINT pk_analysis_info_files
    PRIMARY KEY (analysis_info_id, filename, content_hash),
  CONSTRAINT fk_analysis_info_files_analysis_info_id_analysis_info
    FOREIGN KEY(analysis_info_id) REFERENCES analysis_info (id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,
  CONSTRAINT fk_analysis_info_files_content_hash_file_contents
    FOREIGN KEY(content_hash) REFERENCES file_contents (content_hash)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED
);
INSERT INTO checkers (analyzer_name, checker_name, severity)
  VALUES ('__FAKE__', '__FAKE__', 0);
INSERT INTO checkers (analyzer_name, checker_name, severity)
  VALUES ('UNKNOWN', 'NOT FOUND', 0);
INSERT INTO alembic_version (version_num) VALUES ('feb5f01b5b52');
)sql";

// The product database at revision feb5f01b5b52, PostgreSQL.
constexpr std::string_view kProductSchemaPostgresql = R"sql(
CREATE TYPE detection_status AS ENUM (
  'new', 'unresolved', 'resolved', 'reopened', 'off', 'unavailable');
CREATE TYPE extended_data_type AS ENUM ('note', 'macro', 'fixit');
CREATE TYPE review_status AS ENUM (
  'unreviewed', 'confirmed', 'false_positive', 'intentional');
CREATE TABLE alembic_version (
  version_num character varying(32) NOT NULL,
  CONSTRAINT alembic_version_pkc PRIMARY KEY (version_num)
);
CREATE TABLE checker_set (
  id serial NOT NULL,
  hash_digest character varying NOT NULL,
  CONSTRAINT pk_checker_set PRIMARY KEY (id),
  CONSTRAINT uq_checker_set_hash_digest UNIQUE (hash_digest)
);
CREATE TABLE analysis_info (
  id serial NOT NULL,
  analyzer_command bytea,
  checker_set_id integer,
  CONSTRAINT pk_analysis_info PRIMARY KEY (id),
  CONSTRAINT fk_analysis_info_checker_set_id_checker_set
    FOREIGN KEY (checker_set_id) REFERENCES checker_set(id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED
);
CREATE TABLE file_contents (
  content_hash character varying NOT NULL,
  content bytea,
  blame_info bytea,
  CONSTRAINT pk_file_contents PRIMARY KEY (content_hash)
);
CREATE TABLE analysis_info_files (
  analysis_info_id integer NOT NULL,
  filename character varying NOT NULL,
  content_hash character varying NOT NULL,
  CONSTRAINT pk_analysis_info_files
    PRIMARY KEY (analysis_info_id, filename, content_hash),
  CONSTRAINT fk_analysis_info_files_analysis_info_id_analysis_info
    FOREIGN KEY (analysis_info_id) REFERENCES analysis_info(id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,
  CONSTRAINT fk_analysis_info_files_content_hash_file_contents
    FOREIGN KEY (content_hash) REFERENCES file_contents(content_hash)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED
);
CREATE TABLE runs (
  id serial NOT NULL,
  date timestamp without time zone,
  duration integer,
  name character varying,
  version character varying,
  can_delete boolean DEFAULT true NOT NULL,
  CONSTRAINT pk_runs PRIMARY KEY (id),
  CONSTRAINT uq_runs_name UNIQUE (name)
);
CREATE TABLE run_histories (
  id serial NOT NULL,
  run_id integer,
  version_tag character varying,
  "user" character varying NOT NULL,
  "time" timestamp without time zone NOT NULL,
  cc_version character varying,
  description character varying,
  CONSTRAINT pk_run_histories PRIMARY KEY (id),
  CONSTRAINT uq_run_histories_run_id UNIQUE (run_id, version_tag),
  CONSTRAINT fk_run_histories_run_id_runs
    FOREIGN KEY (run_id) REFERENCES runs(id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED
);
CREATE TABLE analyzer_statistics (
  id serial NOT NULL,
  run_history_id integer,
  analyzer_type character varying,
  version bytea,
  successful integer,
  failed integer,
  failed_files bytea,
  CONSTRAINT pk_analyzer_statistics PRIMARY KEY (id),
  CONSTRAINT fk_analyzer_statistics_run_history_id_run_histories
    FOREIGN KEY (run_history_id) REFERENCES run_histories(id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED
);
CREATE TABLE checkers (
  id serial NOT NULL,
  analyzer_name character varying,
  checker_name character varying,
  severity integer,
  CONSTRAINT pk_checkers PRIMARY KEY (id),
  CONSTRAINT uq_checkers_analyzer_name UNIQUE (analyzer_name, checker_name)
);
CREATE TABLE checker_set_items (
  checker_set_id integer NOT NULL,
  checker_id integer NOT NULL,
  enabled boolean NOT NULL,
  CONSTRAINT pk_checker_set_items PRIMARY KEY (checker_set_id, checker_id),
  CONSTRAINT fk_checker_set_items_checker_id_checkers
    FOREIGN KEY (checker_id) REFERENCES checkers(id)
    ON DELETE RESTRICT DEFERRABLE INITIALLY DEFERRED,
  CONSTRAINT fk_checker_set_items_checker_set_id_checker_set
    FOREIGN KEY (checker_set_id) REFERENCES checker_set(id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED
);
CREATE TABLE cleanup_plans (
  id serial NOT NULL,
  name character varying NOT NULL,
  due_date timestamp without time zone,
  description character varying,
  closed_at timestamp without time zone,
  CONSTRAINT pk_cleanup_plans PRIMARY KEY (id),
  CONSTRAINT uq_cleanup_plans_name UNIQUE (name)
);
CREATE TABLE cleanup_plan_report_hashes (
  cleanup_plan_id integer,
  bug_hash character varying NOT NULL,
  CONSTRAINT pk_cleanup_plan_report_hashes PRIMARY KEY (bug_hash),
  CONSTRAINT fk_cleanup_plan_report_hashes_cleanup_plan_id_cleanup_plans
    FOREIGN KEY (cleanup_plan_id) REFERENCES cleanup_plans(id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED
);
CREATE TABLE comments (
  id serial NOT NULL,
  bug_hash character varying NOT NULL,
  author character varying NOT NULL,
  message bytea NOT NULL,
  created_at timestamp without time zone NOT NULL,
  kind integer DEFAULT 0 NOT NULL,
  CONSTRAINT pk_comments PRIMARY KEY (id)
);
CREATE TABLE files (
  id serial NOT NULL,
  filepath character varying NOT NULL,
  filename character varying NOT NULL,
  content_hash character varying,
  remote_url character varying,
  tracking_branch character varying,
  CONSTRAINT pk_files PRIMARY KEY (id),
  CONSTRAINT uq_files_filepath UNIQUE (filepath, content_hash),
  CONSTRAINT fk_files_content_hash_file_contents
    FOREIGN KEY (content_hash) REFERENCES file_contents(content_hash)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED
);
CREATE TABLE filter_presets (
  id serial NOT NULL,
  preset_name character varying(100) NOT NULL,
  report_filter json NOT NULL,
  CONSTRAINT pk_filter_presets PRIMARY KEY (id),
  CONSTRAINT uq_filter_presets_preset_name UNIQUE (preset_name)
);
CREATE TABLE reports (
  id bigserial NOT NULL,
  file_id integer,
  run_id integer,
  bug_id character varying,
  line integer,
  "column" integer,
  checker_message character varying,
  detection_status detection_status,
  detected_at timestamp without time zone NOT NULL,
  fixed_at timestamp without time zone,
  path_length integer,
  review_status review_status DEFAULT 'unreviewed'::review_status,
  review_status_author character varying,
  review_status_date timestamp without time zone,
  review_status_is_in_source boolean DEFAULT false,
  review_status_message bytea,
  checker_id integer NOT NULL,
  CONSTRAINT pk_reports PRIMARY KEY (id),
  CONSTRAINT fk_reports_checker_id_checkers
    FOREIGN KEY (checker_id) REFERENCES checkers(id) ON DELETE RESTRICT,
  CONSTRAINT fk_reports_file_id_files
    FOREIGN KEY (file_id) REFERENCES files(id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,
  CONSTRAINT fk_reports_run_id_runs
    FOREIGN KEY (run_id) REFERENCES runs(id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED
);
CREATE TABLE report_analysis_info (
  report_id bigint NOT NULL,
  analysis_info_id integer NOT NULL,
  CONSTRAINT pk_report_analysis_info
    PRIMARY KEY (report_id, analysis_info_id),
  CONSTRAINT fk_report_analysis_info_analysis_info_id_analysis_info
    FOREIGN KEY (analysis_info_id) REFERENCES analysis_info(id),
  CONSTRAINT fk_report_analysis_info_report_id_reports
    FOREIGN KEY (report_id) REFERENCES reports(id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED
);
CREATE TABLE report_annotations (
  report_id bigint NOT NULL,
  key character varying NOT NULL,
  value character varying NOT NULL,
  CONSTRAINT pk_report_annotations PRIMARY KEY (report_id, key),
  CONSTRAINT fk_report_annotations_report_id_reports
    FOREIGN KEY (report_id) REFERENCES reports(id) ON DELETE CASCADE
);
CREATE TABLE report_path_data (
  report_id bigint NOT NULL,
  path_data bytea NOT NULL,
  CONSTRAINT pk_report_path_data PRIMARY KEY (report_id),
  CONSTRAINT fk_report_path_data_report_id_reports
    FOREIGN KEY (report_id) REFERENCES reports(id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED
);
CREATE TABLE report_path_data_files (
  report_path_data_id bigint NOT NULL,
  file_id integer NOT NULL,
  CONSTRAINT pk_report_path_data_files
    PRIMARY KEY (report_path_data_id, file_id),
  CONSTRAINT fk_report_path_data_files_file_id_files
    FOREIGN KEY (file_id) REFERENCES files(id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,
  CONSTRAINT fk_report_path_data_files_report_path_data_id_report_path_data
    FOREIGN KEY (report_path_data_id) REFERENCES report_path_data(report_id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED
);
CREATE TABLE review_statuses (
  bug_hash character varying NOT NULL,
  status review_status NOT NULL,
  author character varying NOT NULL,
  message bytea NOT NULL,
  date timestamp without time zone NOT NULL,
  CONSTRAINT pk_review_statuses PRIMARY KEY (bug_hash)
);
CREATE TABLE run_history_analysis_info (
  run_history_id integer NOT NULL,
  analysis_info_id integer NOT NULL,
  CONSTRAINT pk_run_history_analysis_info
    PRIMARY KEY (run_history_id, analysis_info_id),
  CONSTRAINT fk_run_history_analysis_info_analysis_info_id_analysis_info
    FOREIGN KEY (analysis_info_id) REFERENCES analysis_info(id),
  CONSTRAINT fk_run_history_analysis_info_run_history_id_run_histories
    FOREIGN KEY (run_history_id) REFERENCES run_histories(id)
    ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED
);
CREATE TABLE run_locks (
  name character varying NOT NULL,
  locked_at timestamp without time zone NOT NULL,
  username character varying,
  CONSTRAINT pk_run_locks PRIMARY KEY (name)
);
CREATE TABLE source_components (
  name character varying NOT NULL,
  value bytea NOT NULL,
  description text,
  username character varying,
  CONSTRAINT pk_source_components PRIMARY KEY (name)
);
CREATE TABLE source_component_files (
  source_component_name character varying NOT NULL,
  file_id integer NOT NULL,
  CONSTRAINT pk_source_component_files
    PRIMARY KEY (source_component_name, file_id),
  CONSTRAINT fk_source_component_files_file_id_files
    FOREIGN KEY (file_id) REFERENCES files(id) ON DELETE CASCADE,
  CONSTRAINT fk_source_component_files_source_component_name_source__e30b
    FOREIGN KEY (source_component_name) REFERENCES source_components(name)
    ON DELETE CASCADE
);
CREATE INDEX ix_analyzer_statistics_run_history_id
  ON analyzer_statistics USING btree (run_history_id);
CREATE INDEX ix_checkers_severity ON checkers USING btree (severity);
CREATE INDEX ix_cleanup_plan_report_hashes_cleanup_plan_id
  ON cleanup_plan_report_hashes USING btree (cleanup_plan_id);
CREATE INDEX ix_comments_bug_hash ON comments USING btree (bug_hash);
CREATE INDEX ix_files_content_hash ON files USING btree (content_hash);
CREATE INDEX ix_report_analysis_info_report_id
  ON report_analysis_info USING btree (report_id);
CREATE INDEX ix_report_annotations_report_id
  ON report_annotations USING btree (report_id);
CREATE INDEX ix_report_path_data_files_file_id
  ON report_path_data_files USING btree (file_id);
CREATE INDEX ix_report_path_data_files_report_path_data_id
  ON report_path_data_files USING btree (report_path_data_id);
CREATE INDEX ix_reports_bug_id ON reports USING btree (bug_id);
CREATE INDEX ix_reports_checker_id ON reports USING btree (checker_id);
CREATE INDEX ix_reports_file_id ON reports USING btree (file_id);
CREATE INDEX ix_reports_run_id ON reports USING btree (run_id);
CREATE INDEX ix_run_histories_run_id ON run_histories USING btree (run_id);
CREATE INDEX ix_run_history_analysis_info_run_history_id
  ON run_history_analysis_info USING btree (run_history_id);
INSERT INTO checkers (analyzer_name, checker_name, severity)
  VALUES ('__FAKE__', '__FAKE__', 0);
INSERT INTO checkers (analyzer_name, checker_name, severity)
  VALUES ('UNKNOWN', 'NOT FOUND', 0);
INSERT INTO alembic_version (version_num) VALUES ('feb5f01b5b52');
)sql";
