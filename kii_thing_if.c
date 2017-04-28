#include "kii_thing_if.h"
#include "kii_thing_if_environment_impl.h"
#include "kii_hidden.h"

#include <kii.h>

#include <stdio.h>
#include <string.h>


/* If your environment does not have assert, you must set
   KII_THING_IF_NOASSERT define. */
#ifdef KII_THING_IF_NOASSERT
  #define M_KII_THING_IF_ASSERT(s)
#else
  #include <assert.h>
  #define M_KII_THING_IF_ASSERT(s) assert(s)
#endif

#define EVAL(f, v) f(v)
#define TOSTR(s) #s
#define ULONG_MAX_STR EVAL(TOSTR, ULONG_MAX)
#define ULONGBUFSIZE (sizeof(ULONG_MAX_STR) / sizeof(char))

#define CONST_STRLEN(str) sizeof(str) - 1

#define APPEND_BODY_CONST(kii, str) kii_api_call_append_body(kii, str, CONST_STRLEN(str))
#define APPEND_BODY(kii, str) kii_api_call_append_body(kii, str, strlen(str))

#define APP_PATH "api/apps"
#define OAUTH_PATH "oauth2/token"
#define THING_IF_APP_PATH "thing-if/apps/"
#define ONBOARDING_PATH "/onboardings"
#define TARGET_PART "/targets/thing:"
#define COMMAND_PART "/commands/"
#define RESULTS_PART "/action-results"
#define STATES_PART "/states"
#define CONTENT_TYPE_VENDOR_THING_ID "application/vnd.kii.OnboardingWithVendorThingIDByThing+json"
#define CONTENT_TYPE_THING_ID "application/vnd.kii.OnboardingWithThingIDByThing+json"
#define CONTENT_TYPE_JSON "application/json"

#define THING_IF_INFO "sn=tic;sv=0.9.6"

typedef enum prv_bool_t {
    TRUE,
    FALSE
} prv_bool_t;

typedef enum prv_get_key_and_value_t {
    PRV_GET_KEY_AND_VALUE_SUCCESS,
    PRV_GET_KEY_AND_VALUE_FAIL,
    PRV_GET_KEY_AND_VALUE_FINISH
} prv_get_key_and_value_t;

static int prv_append_key_value(
        kii_t* kii,
        const char* key,
        const char* value,
        prv_bool_t is_successor,
        prv_bool_t is_string)
{
    if (key == NULL) {
        M_KII_LOG(kii->kii_core.logger_cb("key not specified.\n"));
        return -1;
    }
    if (value == NULL) {
        M_KII_LOG(
            kii->kii_core.logger_cb("value not specified for key: %s.\n", key));
        return -1;
    }

    if (is_successor == TRUE) {
        if (kii_api_call_append_body(kii, ",", CONST_STRLEN(",") != 0)) {
            M_KII_LOG(kii->kii_core.logger_cb(
                "request size overflowed: (%s, %s).\n", key, value));
            return -1;
        }
    }
    /* Write key. */
    if (kii_api_call_append_body(kii, "\"", CONST_STRLEN("\"")) != 0 ||
            kii_api_call_append_body(kii, key, strlen(key)) != 0 ||
            kii_api_call_append_body(kii, "\":", CONST_STRLEN("\":")) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb(
            "request size overflowed: (%s, %s).\n", key, value));
        return -1;
    }

    /* Write value. */
    if (is_string == TRUE) {
        if (kii_api_call_append_body(kii, "\"", CONST_STRLEN("\"")) != 0 ||
                kii_api_call_append_body(kii, value, strlen(value)) != 0 ||
                kii_api_call_append_body(kii, "\"", CONST_STRLEN("\"")) != 0) {
            M_KII_LOG(kii->kii_core.logger_cb(
                    "request size overflowed: (%s, %s).\n", key, value));
            return -1;
        }
    } else {
        if (kii_api_call_append_body(kii, value, strlen(value)) != 0) {
            M_KII_LOG(kii->kii_core.logger_cb(
                    "request size overflowed: (%s, %s).\n", key, value));
            return -1;
        }
    }
    return 0;
}

static int prv_append_key_value_string(
        kii_t* kii,
        const char* key,
        const char* value,
        prv_bool_t is_successor)
{
    return prv_append_key_value(kii, key, value, is_successor, TRUE);
}

static int prv_append_key_value_string_optional(
        kii_t* kii,
        const char* key,
        const char* value,
        prv_bool_t is_successor)
{
    if (key == NULL || value == NULL) {
        return 0;
    }
    return prv_append_key_value(kii, key, value, is_successor, TRUE);
}

static int prv_append_key_value_object_optional(
        kii_t* kii,
        const char* key,
        const char* value,
        prv_bool_t is_successor)
{
    if (key == NULL || value == NULL) {
        return 0;
    }
    return prv_append_key_value(kii, key, value, is_successor, FALSE);
}

static int prv_append_key_value_bool(
        kii_t* kii,
        const char* key,
        kii_bool_t value,
        prv_bool_t is_successor)
{
    return prv_append_key_value(
            kii,
            key,
            value == KII_TRUE ? "true" : "false",
            is_successor,
            FALSE);
}

static kii_json_parse_result_t prv_kii_thing_if_json_read_object(
        kii_t* kii,
        const char* json_string,
        size_t json_string_size,
        kii_json_field_t *fields)
{
    kii_json_t kii_json;
    kii_json_parse_result_t retval;
    char error_message[50];

    memset(&kii_json, 0, sizeof(kii_json));
    kii_json.error_string_buff = error_message;
    kii_json.error_string_length =
        sizeof(error_message) / sizeof(error_message[0]);

    kii_json.resource = &(kii->kii_json_resource);
    retval = kii_json_read_object(&kii_json, json_string, json_string_size,
                fields);

    switch (retval) {
        case KII_JSON_PARSE_SUCCESS:
        case KII_JSON_PARSE_PARTIAL_SUCCESS:
            break;
        default:
            M_KII_LOG(kii->kii_core.logger_cb(
                    "fail to parse json: result=%d, message=%s\n",
                    retval, kii_json.error_string_buff));
            break;
    }
    return retval;
}


static int prv_thing_if_parse_onboarding_response(kii_t* kii)
{
    kii_json_field_t fields[3];

    M_KII_THING_IF_ASSERT(kii != NULL);

    memset(fields, 0, sizeof(fields));
    fields[0].name = "thingID";
    fields[0].type = KII_JSON_FIELD_TYPE_STRING;
    fields[0].field_copy.string = kii->kii_core.author.author_id;
    fields[0].field_copy_buff_size = sizeof(kii->kii_core.author.author_id) /
            sizeof(kii->kii_core.author.author_id[0]);
    fields[1].name = "accessToken";
    fields[1].type = KII_JSON_FIELD_TYPE_STRING;
    fields[1].field_copy.string = kii->kii_core.author.access_token;
    fields[1].field_copy_buff_size = sizeof(kii->kii_core.author.access_token) /
            sizeof(kii->kii_core.author.access_token[0]);
    fields[2].name = NULL;

    if (prv_kii_thing_if_json_read_object(kii, kii->kii_core.response_body,
                    strlen(kii->kii_core.response_body), fields) !=
            KII_JSON_PARSE_SUCCESS) {
        return -1;
    }

    return 0;
}

static kii_bool_t prv_init_kii_thing_if(
        kii_thing_if_t* kii_thing_if,
        const char* app_id,
        const char* app_key,
        const char* app_host,
        kii_thing_if_command_handler_resource_t* command_handler_resource,
        kii_thing_if_state_updater_resource_t* state_updater_resource,
        KII_JSON_RESOURCE_CB resource_cb)
{
    M_KII_THING_IF_ASSERT(kii_thing_if != NULL);
    M_KII_THING_IF_ASSERT(app_id != NULL);
    M_KII_THING_IF_ASSERT(app_key != NULL);
    M_KII_THING_IF_ASSERT(app_host != NULL);
    M_KII_THING_IF_ASSERT(command_handler_resource != NULL);
    M_KII_THING_IF_ASSERT(state_updater_resource != NULL);

    memset(kii_thing_if, 0x00, sizeof(kii_thing_if_t));
    memset(command_handler_resource->mqtt_buffer, 0x00,
            command_handler_resource->mqtt_buffer_size);
    memset(command_handler_resource->buffer, 0x00,
            command_handler_resource->buffer_size);
    memset(state_updater_resource->buffer, 0x00,
            state_updater_resource->buffer_size);

    /* Initialize command_handler */
    if (_kii_init_with_info(&kii_thing_if->command_handler, app_host, app_id,
                    app_key, THING_IF_INFO) != 0) {
        return KII_FALSE;
    }
    kii_thing_if->command_handler.kii_core.http_context.buffer =
        command_handler_resource->buffer;
    kii_thing_if->command_handler.kii_core.http_context.buffer_size =
        command_handler_resource->buffer_size;

    kii_thing_if->command_handler.mqtt_buffer =
        command_handler_resource->mqtt_buffer;
    kii_thing_if->command_handler.mqtt_buffer_size =
        command_handler_resource->mqtt_buffer_size;

    kii_thing_if->command_handler.kii_json_resource_cb = resource_cb;

    kii_thing_if->action_handler = command_handler_resource->action_handler;
    kii_thing_if->state_handler_for_command_reaction=
        command_handler_resource->state_handler;
    kii_thing_if->custom_push_handler =
        command_handler_resource->custom_push_handler;

    kii_thing_if->command_handler.app_context = (void*)kii_thing_if;

    /* Initialize state_updater */
    if (_kii_init_with_info(&kii_thing_if->state_updater, app_host, app_id,
                    app_key, THING_IF_INFO) != 0) {
        return KII_FALSE;
    }
    kii_thing_if->state_updater.kii_core.http_context.buffer =
        state_updater_resource->buffer;
    kii_thing_if->state_updater.kii_core.http_context.buffer_size =
        state_updater_resource->buffer_size;

    kii_thing_if->state_updater.kii_json_resource_cb = resource_cb;

    kii_thing_if->state_handler_for_period =
        state_updater_resource->state_handler;
    kii_thing_if->state_update_period = state_updater_resource->period;

    kii_thing_if->state_updater.app_context = (void*)kii_thing_if;

    /* setup command handler callbacks. */
    kii_thing_if->command_handler.kii_core.http_context.connect_cb =
        socket_connect_cb_impl;
    kii_thing_if->command_handler.kii_core.http_context.send_cb =
        socket_send_cb_impl;
    kii_thing_if->command_handler.kii_core.http_context.recv_cb =
        socket_recv_cb_impl;
    kii_thing_if->command_handler.kii_core.http_context.close_cb =
        socket_close_cb_impl;
    kii_thing_if->command_handler.mqtt_socket_connect_cb = mqtt_connect_cb_impl;
    kii_thing_if->command_handler.mqtt_socket_send_cb = mqtt_send_cb_impl;
    kii_thing_if->command_handler.mqtt_socket_recv_cb = mqtt_recv_cb_impl;
    kii_thing_if->command_handler.mqtt_socket_close_cb = mqtt_close_cb_impl;
    kii_thing_if->command_handler.task_create_cb = task_create_cb_impl;
    kii_thing_if->command_handler.delay_ms_cb = delay_ms_cb_impl;
    kii_thing_if->command_handler.kii_core.logger_cb = logger_cb_impl;

    /* setup state updater callbacks. */
    kii_thing_if->state_updater.kii_core.http_context.connect_cb =
        socket_connect_cb_impl;
    kii_thing_if->state_updater.kii_core.http_context.send_cb =
        socket_send_cb_impl;
    kii_thing_if->state_updater.kii_core.http_context.recv_cb =
        socket_recv_cb_impl;
    kii_thing_if->state_updater.kii_core.http_context.close_cb =
        socket_close_cb_impl;
    kii_thing_if->state_updater.task_create_cb = task_create_cb_impl;
    kii_thing_if->state_updater.delay_ms_cb = delay_ms_cb_impl;
    kii_thing_if->state_updater.kii_core.logger_cb = logger_cb_impl;

    return KII_TRUE;
}

kii_bool_t init_kii_thing_if(
        kii_thing_if_t* kii_thing_if,
        const char* app_id,
        const char* app_key,
        const char* app_host,
        kii_thing_if_command_handler_resource_t* command_handler_resource,
        kii_thing_if_state_updater_resource_t* state_updater_resource,
        KII_JSON_RESOURCE_CB resource_cb)
{
    return prv_init_kii_thing_if(kii_thing_if, app_id, app_key, app_host,
            command_handler_resource, state_updater_resource, resource_cb);
}

static int prv_kii_thing_if_get_key_and_value_from_json(
        kii_t* kii,
        const char* json_string,
        size_t json_string_len,
        char** out_key,
        char** out_value,
        size_t* out_key_len,
        size_t* out_value_len)
{
    jsmn_parser parser;
    int parse_result = JSMN_ERROR_NOMEM;
#ifdef KII_JSON_FIXED_TOKEN_NUM
    jsmntok_t tokens[KII_JSON_FIXED_TOKEN_NUM];
    size_t tokens_num = sizeof(tokens) / sizeof(tokens[0]);
#else
    jsmntok_t* tokens = kii->kii_json_resource.tokens;
    size_t tokens_num = kii->kii_json_resource.tokens_num;
#endif

    jsmn_init(&parser);

    parse_result = jsmn_parse(&parser, json_string, json_string_len, tokens,
            tokens_num);
    if (parse_result >= 0) {
        if (tokens[0].type != JSMN_OBJECT) {
            M_KII_LOG(kii->kii_core.logger_cb("action must be json object.\n"));
            return -1;
        }
        if (tokens[1].type != JSMN_STRING) {
            M_KII_LOG(kii->kii_core.logger_cb("invalid json object.\n"));
            return -1;
        }
        *out_key = (char*)(json_string + tokens[1].start);
        *out_key_len = tokens[1].end - tokens[1].start;
        *out_value = (char*)(json_string + tokens[2].start);
        *out_value_len = tokens[2].end - tokens[2].start;
        return 0;
    } else if (parse_result == JSMN_ERROR_NOMEM) {
        M_KII_LOG(kii->kii_core.logger_cb(
                "Not enough tokens were provided.\n"));
        return -1;
    } else if (parse_result == JSMN_ERROR_INVAL) {
        M_KII_LOG(kii->kii_core.logger_cb(
                "Invalid character inside JSON string.\n"));
        return -1;
    } else if (parse_result == JSMN_ERROR_PART) {
        M_KII_LOG(kii->kii_core.logger_cb(
                "The string is not a full JSON packet, more bytes expected.\n"));
        return -1;
    } else {
        M_KII_LOG(kii->kii_core.logger_cb("Unexpected error.\n"));
        return -1;
    }
}

static kii_bool_t prv_writer(kii_t* kii, const char* buff)
{
    if (kii_api_call_append_body(kii, buff, strlen(buff)) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("request size overflowed.\n"));
        return KII_FALSE;
    }
    return KII_TRUE;
}

static kii_bool_t prv_send_state(kii_t* kii)
{
    char resource_path[256];

    if (sizeof(resource_path) / sizeof(resource_path[0]) <=
            CONST_STRLEN(THING_IF_APP_PATH) +
            strlen(kii->kii_core.app_id) + CONST_STRLEN(TARGET_PART) +
            strlen(kii->kii_core.author.author_id) +
            CONST_STRLEN(STATES_PART)) {
        M_KII_LOG(kii->kii_core.logger_cb(
                "resource path is longer than expected.\n"));
        return KII_FALSE;
    }

    resource_path[0] = '\0';
    strcat(resource_path, THING_IF_APP_PATH);
    strcat(resource_path, kii->kii_core.app_id);
    strcat(resource_path, TARGET_PART);
    strcat(resource_path, kii->kii_core.author.author_id);
    strcat(resource_path, STATES_PART);

    if (kii_api_call_start(kii, "PUT", resource_path, CONTENT_TYPE_JSON,
                    KII_TRUE) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("fail to start api call.\n"));
        return KII_FALSE;
    }
    if (((kii_thing_if_t*)kii->app_context)->state_handler_for_command_reaction(kii,
                    prv_writer) == KII_FALSE) {
        M_KII_LOG(kii->kii_core.logger_cb("fail to start api call.\n"));
        return KII_FALSE;
    }
    if (kii_api_call_run(kii) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("fail to run api.\n"));
        return KII_FALSE;
    }

    return KII_TRUE;
}

static prv_get_key_and_value_t get_key_and_value_at_index(
        kii_t* kii,
        const char* array_str,
        size_t array_len,
        size_t index,
        char** key,
        size_t* key_len,
        char** value,
        size_t* value_len)
{
    kii_json_field_t item[2];
    char index_str[ULONGBUFSIZE];
    memset(item, 0x00, sizeof(item));
    item[0].path = index_str;
    item[0].type = KII_JSON_FIELD_TYPE_OBJECT;
    item[0].field_copy.string = NULL;
    item[0].result = KII_JSON_FIELD_PARSE_SUCCESS;
    item[1].path = NULL;
    sprintf(index_str, "/[%lu]", index);

    switch (prv_kii_thing_if_json_read_object(
                kii,
                array_str,
                array_len,
                item)) {
        case KII_JSON_PARSE_SUCCESS:
            if (prv_kii_thing_if_get_key_and_value_from_json(
                        kii,
                        array_str + item[0].start,
                        item[0].end - item[0].start,
                        key,
                        value,
                        key_len,
                        value_len) != 0) {
                M_KII_LOG(kii->kii_core.logger_cb("fail to parse item."))
                return PRV_GET_KEY_AND_VALUE_FAIL;
            }
            return PRV_GET_KEY_AND_VALUE_SUCCESS;
        case KII_JSON_PARSE_PARTIAL_SUCCESS:
            /* This must be end of array. */
            return PRV_GET_KEY_AND_VALUE_FINISH;
        case KII_JSON_PARSE_ROOT_TYPE_ERROR:
        case KII_JSON_PARSE_INVALID_INPUT:
        default:
            M_KII_LOG(kii->kii_core.logger_cb("unexpected error.\n"));
            return PRV_GET_KEY_AND_VALUE_FAIL;
    }
}

static void handle_command(kii_t* kii, char* buffer, size_t buffer_size)
{
    char* alias_actions_str = NULL;
    size_t alias_actions_len = 0;
    size_t alias_index = 0;
    prv_get_key_and_value_t alias_result;

    /*
      1. Get start position of alias action array
      2. Start to make request to update command result with kii_api_call_start.
    */
    {
        kii_json_field_t fields[3];
        char resource_path[256];
        memset(fields, 0x00, sizeof(fields));
        fields[0].path = "/commandID";
        fields[0].type = KII_JSON_FIELD_TYPE_STRING;
        fields[0].field_copy.string = NULL;
        fields[1].path = "/actions";
        fields[1].type = KII_JSON_FIELD_TYPE_ARRAY;
        fields[1].field_copy.string = NULL;
        fields[2].path = NULL;

        switch(prv_kii_thing_if_json_read_object(
                kii, buffer, buffer_size, fields)) {
            case KII_JSON_PARSE_SUCCESS:
                break;
            case KII_JSON_PARSE_PARTIAL_SUCCESS:
                if (fields[0].result != KII_JSON_FIELD_PARSE_SUCCESS) {
                    /* no command ID. */
                    return;
                }
                break;
            default:
                M_KII_LOG(kii->kii_core.logger_cb(
                        "fail to parse received message.\n"));
                return;
        }

        if (sizeof(resource_path) / sizeof(resource_path[0]) <=
                CONST_STRLEN(THING_IF_APP_PATH) +
                strlen(kii->kii_core.app_id) + CONST_STRLEN(TARGET_PART) +
                strlen(kii->kii_core.author.author_id) +
                CONST_STRLEN(COMMAND_PART) +
                (fields[2].end - fields[2].start - 1) +
                CONST_STRLEN(RESULTS_PART)) {
            M_KII_LOG(kii->kii_core.logger_cb(
                    "resource path is longer than expected.\n"));
            return;
        }

        resource_path[0] = '\0';
        strcat(resource_path, THING_IF_APP_PATH);
        strcat(resource_path, kii->kii_core.app_id);
        strcat(resource_path, TARGET_PART);
        strcat(resource_path, kii->kii_core.author.author_id);
        strcat(resource_path, COMMAND_PART);
        strncat(resource_path, buffer + fields[2].start,
                fields[2].end - fields[2].start);
        strcat(resource_path, RESULTS_PART);
        /* TODO: Check properties. */

        if (kii_api_call_start(kii, "PUT", resource_path, "application/json",
                        KII_TRUE) != 0) {
            M_KII_LOG(kii->kii_core.logger_cb("fail to start api call.\n"));
            return;
        }

        alias_actions_str = buffer + fields[1].start;
        alias_actions_len = fields[1].end - fields[1].start;
    }

    if (kii_api_call_append_body(kii, "{\"actionResults\":[",
                    CONST_STRLEN("{\"actionResults\":[")) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("request size overflowed.\n"));
        return;
    }

    for (alias_index = 0, alias_result = PRV_GET_KEY_AND_VALUE_SUCCESS;
            alias_result == PRV_GET_KEY_AND_VALUE_SUCCESS;
            ++alias_index) {
        KII_THING_IF_ACTION_HANDLER handler =
            ((kii_thing_if_t*)kii->app_context)->action_handler;
        char* alias_name;
        char* actions;
        size_t alias_name_len, actions_len;
        alias_result = get_key_and_value_at_index(
                kii,
                alias_actions_str,
                alias_actions_len,
                alias_index++,
                &alias_name,
                &alias_name_len,
                &actions,
                &actions_len);
        switch (alias_result) {
            case PRV_GET_KEY_AND_VALUE_FAIL:
                M_KII_LOG(kii->kii_core.logger_cb("fail to get alias.\n"));
                return;
            case PRV_GET_KEY_AND_VALUE_SUCCESS:
            {
                char alias_name_swap;
                size_t action_index;
                prv_get_key_and_value_t action_result;

                alias_name_swap = alias_name[alias_name_len];
                alias_name[alias_name_len] = '\0';

                for (action_index = 0,
                            action_result = PRV_GET_KEY_AND_VALUE_SUCCESS;
                        action_result == PRV_GET_KEY_AND_VALUE_SUCCESS;
                        ++action_index) {
                    char* name;
                    char* value;
                    size_t name_len, value_len;
                    action_result = get_key_and_value_at_index(
                            kii,
                            actions,
                            actions_len,
                            action_index++,
                            &name,
                            &name_len,
                            &value,
                            &value_len);
                    switch (action_result) {
                        case PRV_GET_KEY_AND_VALUE_FAIL:
                            M_KII_LOG(kii->kii_core.logger_cb(
                                    "fail to get action.\n"));
                            return;
                        case PRV_GET_KEY_AND_VALUE_SUCCESS:
                        {
                            char name_swap, value_swap;
                            char error[EMESSAGE_SIZE + 1];
                            kii_bool_t succeeded;
                            name_swap = name[name_len];
                            value_swap = value[value_len];
                            name[name_len] = '\0';
                            value[value_len] = '\0';

                            succeeded =
                              (*handler)(alias_name, name, value, error);

                            if (alias_index > 0 || action_index > 0) {
                                if (APPEND_BODY_CONST(kii, ",") != 0) {
                                    M_KII_LOG(kii->kii_core.logger_cb(
                                            "request size overflowed.\n"));
                                }
                            }
                            if (APPEND_BODY_CONST(kii, "{\"") != 0 ||
                                    APPEND_BODY(kii, name) != 0 ||
                                    APPEND_BODY_CONST(kii, ":{") != 0 ||
                                    prv_append_key_value_bool(
                                        kii,
                                        "succeeded",
                                        succeeded,
                                        FALSE) != 0 ||
                                    prv_append_key_value_string_optional(
                                        kii,
                                        "errorMessage",
                                        succeeded == FALSE ? error : NULL,
                                        TRUE) != 0 ||
                                    APPEND_BODY_CONST(kii, "}}\"") != 0) {
                                M_KII_LOG(kii->kii_core.logger_cb(
                                        "request size overflowed.\n"));
                                return;
                            }
                            break;
                        }
                        case PRV_GET_KEY_AND_VALUE_FINISH:
                            /* finished to parse aliases. */
                            break;
                        default:
                            M_KII_LOG(kii->kii_core.logger_cb(
                                    "unknown result %d.\n",result));
                            M_KII_THING_IF_ASSERT(0);
                            return;
                    }
                }
                alias_name[alias_name_len] = alias_name_swap;
                break;
            }
            case PRV_GET_KEY_AND_VALUE_FINISH:
                /* finished to parse aliases. */
                break;
            default:
                M_KII_LOG(
                    kii->kii_core.logger_cb("unknown result %d.\n",result));
                M_KII_THING_IF_ASSERT(0);
                return;
        }
    }

    if (kii_api_call_append_body(kii, "]}", sizeof("]}") - 1) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("request size overflowed.\n"));
        return;
    }
    if (kii_api_call_run(kii) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("fail to run api.\n"));
        return;
    }

    if (prv_send_state(kii) != KII_FALSE) {
        M_KII_LOG(kii->kii_core.logger_cb("fail to send state.\n"));
    }

    return;
}

static void received_callback(kii_t* kii, char* buffer, size_t buffer_size) {
    kii_bool_t skip = KII_FALSE;
    if (((kii_thing_if_t*)kii->app_context)->custom_push_handler != NULL) {
        KII_THING_IF_CUSTOM_PUSH_HANDLER handler =
            ((kii_thing_if_t*)kii->app_context)->custom_push_handler;
        skip = (*handler)(kii, buffer, buffer_size);
    }
    if (skip == KII_FALSE) {
        handle_command(kii, buffer, buffer_size);
    }
}

static int prv_kii_thing_if_get_anonymous_token(kii_t* kii)
{
    char resource_path[64];
    kii_json_field_t fields[2];

    M_KII_THING_IF_ASSERT(kii);

    if (sizeof(resource_path) / sizeof(resource_path[0]) <=
            CONST_STRLEN(APP_PATH) + CONST_STRLEN("/") +
            strlen(kii->kii_core.app_id) + CONST_STRLEN(OAUTH_PATH)) {
        M_KII_LOG(kii->kii_core.logger_cb(
                "resource path is longer than expected.\n"));
        return -1;
    }
    sprintf(resource_path, "%s/%s/%s", APP_PATH, kii->kii_core.app_id,
            OAUTH_PATH);

    if (kii_api_call_start(kii, "POST", resource_path, "application/json",
                    KII_FALSE) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("fail to start api call.\n"));
    }

    if (kii_api_call_append_body(kii,
                    "{\"grant_type\":\"client_credentials\",\"client_id\":\"",
                    CONST_STRLEN(
                        "{\"grant_type\":\"client_credentials\",\"client_id\":\""))
            != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("request size overflowed.\n"));
        return -1;
    }
    if (kii_api_call_append_body(kii, kii->kii_core.app_id,
                    strlen(kii->kii_core.app_id)) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("request size overflowed.\n"));
        return -1;
    }
    if (kii_api_call_append_body(kii, "\",\"client_secret\": \"",
                    CONST_STRLEN("\",\"client_secret\": \"")) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("request size overflowed.\n"));
        return -1;
    }
    if (kii_api_call_append_body(kii, kii->kii_core.app_key,
                    strlen(kii->kii_core.app_key)) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("request size overflowed.\n"));
        return -1;
    }
    if (kii_api_call_append_body(kii, "\"}", CONST_STRLEN("\"}")) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("request size overflowed.\n"));
        return -1;
    }
    if (kii_api_call_run(kii) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("fail to run api.\n"));
        return -1;
    }

    memset(fields, 0x00, sizeof(fields));
    fields[0].path = "/access_token";
    fields[0].type = KII_JSON_FIELD_TYPE_STRING;
    fields[0].field_copy.string = kii->kii_core.author.access_token;
    fields[0].field_copy_buff_size = sizeof(kii->kii_core.author.access_token) /
            sizeof(kii->kii_core.author.access_token[0]);
    fields[1].path = NULL;

    if (prv_kii_thing_if_json_read_object(kii, kii->kii_core.response_body,
                    strlen(kii->kii_core.response_body), fields)
            != KII_JSON_PARSE_SUCCESS) {
        M_KII_LOG(kii->kii_core.logger_cb("fail to parse received message.\n"));
        return -1;
    }

    return 0;
}

static kii_bool_t prv_onboard_with_vendor_thing_id(
        kii_t* kii,
        const char* vendor_thing_id,
        const char* password,
        const char* thing_type,
        const char* thing_properties,
        const char* firmware_version,
        const char* layout_position)
{
    char resource_path[64];

    if (prv_kii_thing_if_get_anonymous_token(kii) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("fail to get anonymous token.\n"));
        return KII_FALSE;
    }

    if (sizeof(resource_path) / sizeof(resource_path[0]) <=
            CONST_STRLEN(THING_IF_APP_PATH) +
            strlen(kii->kii_core.app_id) + CONST_STRLEN(ONBOARDING_PATH)) {
        M_KII_LOG(kii->kii_core.logger_cb(
                "resource path is longer than expected.\n"));
        return KII_FALSE;
    }
    sprintf(resource_path, "%s%s%s", THING_IF_APP_PATH, kii->kii_core.app_id,
            ONBOARDING_PATH);

    if (kii_api_call_start(kii, "POST", resource_path,
                    CONTENT_TYPE_VENDOR_THING_ID, KII_TRUE) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("fail to start api call.\n"));
    }

    /* Open to write JSON object. */
    if (kii_api_call_append_body(kii, "{", CONST_STRLEN("{")) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("request size overflowed.\n"));
        return KII_FALSE;
    }
    /* Append key value pairs. */
    if (prv_append_key_value_string(
            kii, "vendorThingID", vendor_thing_id, FALSE) != 0 ||
            prv_append_key_value_string(
                kii, "thingPassword", password, TRUE) != 0 ||
            prv_append_key_value_string_optional(
                kii, "thingType", thing_type, TRUE) != 0 ||
            prv_append_key_value_object_optional(
                kii, "thingProperties", thing_properties, TRUE) != 0 ||
            prv_append_key_value_string_optional(
                kii, "firmwareVersion", firmware_version, TRUE) != 0 ||
            prv_append_key_value_string_optional(
                kii, "layoutPosition", layout_position, TRUE) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("request size overflowed.\n"));
        return KII_FALSE;
    }
    /* Close JSON object. */
    if (kii_api_call_append_body(kii, "}", CONST_STRLEN("}")) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("request size overflowed.\n"));
        return KII_FALSE;
    }

    if (kii_api_call_run(kii) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("fail to run api.\n"));
        return KII_FALSE;
    }

    if (prv_thing_if_parse_onboarding_response(kii) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("fail to parse resonse.\n"));
        return KII_FALSE;
    }

    if (kii_push_start_routine(kii, received_callback) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("fail to start routine.\n"));
        return KII_FALSE;
    }

    return KII_TRUE;
}

static void* prv_update_status(void *sdata)
{
    kii_t* kii = (kii_t*)sdata;
    char resource_path[256];

    if (sizeof(resource_path) / sizeof(resource_path[0]) <=
            CONST_STRLEN(THING_IF_APP_PATH) +
            strlen(kii->kii_core.app_id) + CONST_STRLEN(TARGET_PART) +
            strlen(kii->kii_core.author.author_id) +
            CONST_STRLEN(STATES_PART)) {
        M_KII_LOG(kii->kii_core.logger_cb(
                "resource path is longer than expected.\n"));
        return NULL;
    }

    resource_path[0] = '\0';
    strcat(resource_path, THING_IF_APP_PATH);
    strcat(resource_path, kii->kii_core.app_id);
    strcat(resource_path, TARGET_PART);
    strcat(resource_path, kii->kii_core.author.author_id);
    strcat(resource_path, STATES_PART);

    while(1) {
        kii->delay_ms_cb(
            ((kii_thing_if_t*)kii->app_context)->state_update_period * 1000);

        if (kii_api_call_start(kii, "PUT", resource_path, CONTENT_TYPE_JSON,
                        KII_TRUE) != 0) {
            M_KII_LOG(kii->kii_core.logger_cb(
                    "fail to start api call.\n"));
            continue;
        }
        if (((kii_thing_if_t*)kii->app_context)->state_handler_for_period(kii,
                        prv_writer) == KII_FALSE) {
            M_KII_LOG(kii->kii_core.logger_cb(
                    "fail to start api call.\n"));
            continue;
        }
        if (kii_api_call_run(kii) != 0) {
            M_KII_LOG(kii->kii_core.logger_cb("fail to run api.\n"));
            continue;
        }
    }
    return NULL;
}

static kii_bool_t prv_set_author(
        kii_author_t* author,
        const char* thing_id,
        const char* access_token)
{
    if (sizeof(author->author_id) / sizeof(author->author_id[0]) <=
            strlen(thing_id)) {
        return KII_FALSE;
    }
    if (sizeof(author->access_token) / sizeof(author->access_token[0]) <=
            strlen(access_token)) {
        return KII_FALSE;
    }

    strncpy(author->author_id, thing_id,
            sizeof(author->author_id) / sizeof(author->author_id[0]));
    strncpy(author->access_token, access_token,
            sizeof(author->access_token) / sizeof(author->access_token[0]));
    return KII_TRUE;
}

kii_bool_t onboard_with_vendor_thing_id(
        kii_thing_if_t* kii_thing_if,
        const char* vendor_thing_id,
        const char* password,
        const char* thing_type,
        const char* thing_properties,
        const char* firmware_version,
        const char* layout_position)
{
    if (prv_onboard_with_vendor_thing_id(&kii_thing_if->command_handler,
                    vendor_thing_id, password, thing_type,
                    thing_properties, firmware_version, layout_position)
            == KII_FALSE) {
        return KII_FALSE;
    }

    if (prv_set_author(&(kii_thing_if->state_updater.kii_core.author),
                    kii_thing_if->command_handler.kii_core.author.author_id,
                    kii_thing_if->command_handler.kii_core.author.access_token)
            == KII_FALSE) {
        return KII_FALSE;
    }
    kii_thing_if->state_updater.task_create_cb(
            KII_THING_IF_TASK_NAME_STATUS_UPDATE,
            prv_update_status, (void*)&kii_thing_if->state_updater);

    return KII_TRUE;
}

static kii_bool_t prv_onboard_with_thing_id(
        kii_t* kii,
        const char* thing_id,
        const char* password,
        const char* thing_type,
        const char* thing_properties,
        const char* firmware_version,
        const char* layout_position)

{
    char resource_path[64];

    if (prv_kii_thing_if_get_anonymous_token(kii) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("fail to get anonymous token.\n"));
        return KII_FALSE;
    }

    if (sizeof(resource_path) / sizeof(resource_path[0]) <=
            CONST_STRLEN(THING_IF_APP_PATH) +
            strlen(kii->kii_core.app_id) + CONST_STRLEN(ONBOARDING_PATH)) {
        M_KII_LOG(kii->kii_core.logger_cb(
                "resource path is longer than expected.\n"));
        return KII_FALSE;
    }
    sprintf(resource_path, "%s%s%s", THING_IF_APP_PATH, kii->kii_core.app_id,
            ONBOARDING_PATH);

    if (kii_api_call_start(kii, "POST", resource_path, CONTENT_TYPE_THING_ID,
                    KII_TRUE) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb(
            "fail to start api call.\n"));
    }

    /* Open to write JSON object. */
    if (kii_api_call_append_body(kii, "{", CONST_STRLEN("{")) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("request size overflowed.\n"));
        return KII_FALSE;
    }
    /* Append key value pairs. */
    if (prv_append_key_value_string(kii, "thingID", thing_id, FALSE) != 0 ||
            prv_append_key_value_string(
                kii, "thingPassword", password, TRUE) != 0 ||
            prv_append_key_value_string_optional(
                kii, "thingType", thing_type, TRUE) != 0 ||
            prv_append_key_value_object_optional(
                kii, "thingProperties", thing_properties, TRUE) != 0 ||
            prv_append_key_value_string_optional(
                kii, "firmwareVersion", firmware_version, TRUE) != 0 ||
            prv_append_key_value_string_optional(
                kii, "layoutPosition", layout_position, TRUE) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("request size overflowed.\n"));
        return KII_FALSE;
    }
    /* Close JSON object. */
    if (kii_api_call_append_body(kii, "}", CONST_STRLEN("}")) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("request size overflowed.\n"));
        return KII_FALSE;
    }

    if (kii_api_call_run(kii) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("fail to run api.\n"));
        return KII_FALSE;
    }

    if (prv_thing_if_parse_onboarding_response(kii) != 0) {
        M_KII_LOG(kii->kii_core.logger_cb("fail to parse resonse.\n"));
        return KII_FALSE;
    }

    if (kii_push_start_routine(kii, received_callback) != 0) {
        return KII_FALSE;
    }

    return KII_TRUE;
}

kii_bool_t onboard_with_thing_id(
        kii_thing_if_t* kii_thing_if,
        const char* thing_id,
        const char* password,
        const char* thing_type,
        const char* thing_properties,
        const char* firmware_version,
        const char* layout_position)

{
    if (prv_onboard_with_thing_id(&kii_thing_if->command_handler, thing_id,
                    password, thing_type, thing_properties, firmware_version,
                    layout_position) == KII_FALSE) {
        return KII_FALSE;
    }

    if (prv_set_author(&(kii_thing_if->state_updater.kii_core.author),
                    kii_thing_if->command_handler.kii_core.author.author_id,
                    kii_thing_if->command_handler.kii_core.author.access_token)
            == KII_FALSE) {
        return KII_FALSE;
    }
    kii_thing_if->state_updater.task_create_cb(
            KII_THING_IF_TASK_NAME_STATUS_UPDATE,
            prv_update_status, (void*)&kii_thing_if->state_updater);

    return KII_TRUE;
}

kii_bool_t init_kii_thing_if_with_onboarded_thing(
        kii_thing_if_t* kii_thing_if,
        const char* app_id,
        const char* app_key,
        const char* app_host,
        const char* thing_id,
        const char* access_token,
        kii_thing_if_command_handler_resource_t* command_handler_resource,
        kii_thing_if_state_updater_resource_t* state_updater_resource,
        KII_JSON_RESOURCE_CB resource_cb)
{
    if (prv_init_kii_thing_if(kii_thing_if, app_id, app_key, app_host,
                    command_handler_resource, state_updater_resource,
                    resource_cb) == KII_FALSE) {
        return KII_FALSE;
    }

    if (prv_set_author(&kii_thing_if->command_handler.kii_core.author,
                    thing_id, access_token) == KII_FALSE) {
        return KII_FALSE;
    }

    if (prv_set_author(&kii_thing_if->state_updater.kii_core.author,
                    thing_id, access_token) == KII_FALSE) {
        return KII_FALSE;
    }

    if (kii_push_start_routine(&kii_thing_if->command_handler,
                    received_callback) != 0) {
        return KII_FALSE;
    }

    kii_thing_if->state_updater.task_create_cb(
            KII_THING_IF_TASK_NAME_STATUS_UPDATE,
            prv_update_status, (void*)&kii_thing_if->state_updater);


    return KII_TRUE;
}

#ifdef KII_THING_IF_TEST_BUILD

void test_handle_command(kii_t* kii, char* buffer, size_t buffer_size)
{
    handle_command(kii, buffer, buffer_size);
}

#endif /* KII_THING_IF_TEST_BUILD */
