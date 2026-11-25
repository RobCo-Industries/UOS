/* The UOS base command table. Data only. */

#include "dcl.h"

#define N(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

/* Value sets. */
static const char *const k_schemes[] = { "GREEN", "AMBER", "BLUE", "WHITE", NULL };
static const char *const k_restart[] = { "RESTART", NULL };
static const char *const k_programs[] = { "HACK", "TERMINAL", "HOLOTAPE", NULL };
static const char *const k_privs[] = { "SYSTEM", "OPER", "SECURITY", "READALL", NULL };

/* Shared parameters. */
static const dcl_param_t p_file[] = { { "File", DCL_VT_FILE, NULL, 0, 0 } };
static const dcl_param_t p_from_to[] = {
    { "From", DCL_VT_FILE, NULL, 0, 0 },
    { "To", DCL_VT_FILE, NULL, 0, 0 },
};
static const dcl_param_t p_dir[] = { { "Directory", DCL_VT_FILE, NULL, 0, 0 } };
static const dcl_param_t p_topic[] = { { "Topic", DCL_VT_TEXT, NULL, 0, 0 } };
static const dcl_param_t p_program[] = { { "Program", DCL_VT_KEYWORD, k_programs, 0, 0 } };
static const dcl_param_t p_username[] = { { "Username", DCL_VT_TEXT, NULL, 0, 0 } };
static const dcl_param_t p_device[] = {
    { "Device", DCL_VT_TEXT, NULL, 0, 0 },
    { "Action", DCL_VT_TEXT, NULL, 0, 0 },
};
static const dcl_param_t p_tape[] = {
    { "Tape", DCL_VT_TEXT, NULL, 0, 0 },
    { "Entry", DCL_VT_TEXT, NULL, 0, 0 },
};
static const dcl_param_t p_halt[] = { { "Action", DCL_VT_KEYWORD, k_restart, 0, 0 } };
static const dcl_param_t p_message[] = { { "Message", DCL_VT_NUMBER, NULL, 1, 9999 } };
static const dcl_param_t p_to[] = { { "To", DCL_VT_TEXT, NULL, 0, 0 } };

/* Qualifiers. */
static const dcl_qual_t q_directory[] = {
    { "FULL", DCL_QF_NEG, DCL_QV_NONE, DCL_VT_NONE, NULL, 0, 0, NULL },
    { "SIZE", DCL_QF_NEG, DCL_QV_NONE, DCL_VT_NONE, NULL, 0, 0, NULL },
    { "DATE", DCL_QF_NEG, DCL_QV_NONE, DCL_VT_NONE, NULL, 0, 0, NULL },
    { "OUTPUT", 0, DCL_QV_REQUIRED, DCL_VT_FILE, NULL, 0, 0, NULL },
};
static const dcl_qual_t q_type[] = {
    { "PAGE", DCL_QF_NEG, DCL_QV_NONE, DCL_VT_NONE, NULL, 0, 0, NULL },
};
static const dcl_qual_t q_log_confirm[] = {
    { "LOG", DCL_QF_NEG, DCL_QV_NONE, DCL_VT_NONE, NULL, 0, 0, NULL },
    { "CONFIRM", DCL_QF_NEG, DCL_QV_NONE, DCL_VT_NONE, NULL, 0, 0, NULL },
};
static const dcl_qual_t q_create[] = {
    { "DIRECTORY", 0, DCL_QV_NONE, DCL_VT_NONE, NULL, 0, 0, NULL },
    { "LOG", DCL_QF_NEG, DCL_QV_NONE, DCL_VT_NONE, NULL, 0, 0, NULL },
};
static const dcl_qual_t q_terminal[] = {
    { "SCHEME", 0, DCL_QV_REQUIRED, DCL_VT_KEYWORD, k_schemes, 0, 0, NULL },
};
static const dcl_qual_t q_halt[] = {
    { "MAINTENANCE", DCL_QF_NEG, DCL_QV_NONE, DCL_VT_NONE, NULL, 0, 0, NULL },
};
static const dcl_qual_t q_output[] = {
    { "OUTPUT", 0, DCL_QV_REQUIRED, DCL_VT_FILE, NULL, 0, 0, NULL },
};
static const dcl_qual_t q_send[] = {
    { "SUBJECT", 0, DCL_QV_REQUIRED, DCL_VT_TEXT, NULL, 0, 0, NULL },
};
static const dcl_qual_t q_user[] = {
    { "PASSWORD", 0, DCL_QV_NONE, DCL_VT_NONE, NULL, 0, 0, NULL }, /* asked for, masked */
    { "PRIVS", DCL_QF_NEG | DCL_QF_LIST, DCL_QV_REQUIRED, DCL_VT_KEYWORD, k_privs, 0, 0, NULL },
    { "HOME", 0, DCL_QV_REQUIRED, DCL_VT_TEXT, NULL, 0, 0, NULL },
    { "DISABLED", DCL_QF_NEG, DCL_QV_NONE, DCL_VT_NONE, NULL, 0, 0, NULL },
    { "HACKABLE", DCL_QF_NEG, DCL_QV_NONE, DCL_VT_NONE, NULL, 0, 0, NULL },
};

/* Second levels. Fields: name, nreq, nmax, params, nquals, quals, nkeys, keys, key_prompt, privs, help. */
static const dcl_verb_t set_keys[] = {
    { "DEFAULT", 1, 1, p_dir, 0, NULL, 0, NULL, NULL, 0, NULL },
    { "PASSWORD", 0, 0, NULL, 0, NULL, 0, NULL, NULL, 0, NULL },
    { "TERMINAL", 0, 0, NULL, N(q_terminal), q_terminal, 0, NULL, NULL, 0, NULL },
    { "HALT", 0, 1, p_halt, N(q_halt), q_halt, 0, NULL, NULL, DCL_PRIV_OPER, NULL },
};

static const dcl_param_t p_show_device[] = { { "Device", DCL_VT_TEXT, NULL, 0, 0 } };

static const dcl_verb_t show_keys[] = {
    { "DEFAULT", 0, 0, NULL, 0, NULL, 0, NULL, NULL, 0, NULL },
    { "USERS", 0, 0, NULL, 0, NULL, 0, NULL, NULL, 0, NULL },
    { "SYSTEM", 0, 0, NULL, 0, NULL, 0, NULL, NULL, 0, NULL },
    { "TIME", 0, 0, NULL, 0, NULL, 0, NULL, NULL, 0, NULL },
    { "DEVICES", 0, 1, p_show_device, 0, NULL, 0, NULL, NULL, 0, NULL },
    { "TERMINAL", 0, 0, NULL, 0, NULL, 0, NULL, NULL, 0, NULL },
    { "MEMORY", 0, 0, NULL, 0, NULL, 0, NULL, NULL, 0, NULL },
};

static const dcl_verb_t mail_keys[] = {
    { "DIRECTORY", 0, 0, NULL, 0, NULL, 0, NULL, NULL, 0, NULL },
    { "READ", 0, 1, p_message, 0, NULL, 0, NULL, NULL, 0, NULL },
    { "SEND", 1, 1, p_to, N(q_send), q_send, 0, NULL, NULL, 0, NULL },
    { "DELETE", 1, 1, p_message, 0, NULL, 0, NULL, NULL, 0, NULL },
};

static const dcl_verb_t authorize_keys[] = {
    { "ADD", 1, 1, p_username, N(q_user), q_user, 0, NULL, NULL, 0, NULL },
    { "MODIFY", 1, 1, p_username, N(q_user), q_user, 0, NULL, NULL, 0, NULL },
    { "REMOVE", 1, 1, p_username, 0, NULL, 0, NULL, NULL, 0, NULL },
    { "LIST", 0, 1, p_username, 0, NULL, 0, NULL, NULL, 0, NULL },
};

static const dcl_verb_t uos_verbs[] = {
    /* Base system. */
    { "DIRECTORY", 0, 1, p_file, N(q_directory), q_directory, 0, NULL, NULL, 0, "SYS$HELP:DIRECTOR.HLP" },
    { "TYPE", 1, 1, p_file, N(q_type), q_type, 0, NULL, NULL, 0, "SYS$HELP:TYPE.HLP" },
    { "COPY", 2, 2, p_from_to, N(q_log_confirm), q_log_confirm, 0, NULL, NULL, 0, "SYS$HELP:COPY.HLP" },
    { "DELETE", 1, 1, p_file, N(q_log_confirm), q_log_confirm, 0, NULL, NULL, 0, "SYS$HELP:DELETE.HLP" },
    { "RENAME", 2, 2, p_from_to, N(q_log_confirm), q_log_confirm, 0, NULL, NULL, 0, "SYS$HELP:RENAME.HLP" },
    { "CREATE", 1, 1, p_file, N(q_create), q_create, 0, NULL, NULL, 0, "SYS$HELP:CREATE.HLP" },
    { "SET", 0, 0, NULL, 0, NULL, N(set_keys), set_keys, "What", 0, "SYS$HELP:SET.HLP" },
    { "SHOW", 0, 0, NULL, N(q_output), q_output, N(show_keys), show_keys, "What", 0, "SYS$HELP:SHOW.HLP" },
    { "HELP", 0, 1, p_topic, 0, NULL, 0, NULL, NULL, 0, "SYS$HELP:HELP.HLP" },
    { "RUN", 1, 1, p_program, 0, NULL, 0, NULL, NULL, 0, "SYS$HELP:RUN.HLP" },
    { "LOGIN", 0, 1, p_username, 0, NULL, 0, NULL, NULL, 0, "SYS$HELP:LOGIN.HLP" },
    { "LOGOUT", 0, 0, NULL, 0, NULL, 0, NULL, NULL, 0, "SYS$HELP:LOGOUT.HLP" },
    { "ROBCO", 0, 0, NULL, 0, NULL, 0, NULL, NULL, 0, "SYS$HELP:ROBCO.HLP" },
    /* Features. */
    { "DEVICE", 1, 2, p_device, 0, NULL, 0, NULL, NULL, 0, "SYS$HELP:DEVICE.HLP" },
    { "HOLOTAPE", 0, 2, p_tape, 0, NULL, 0, NULL, NULL, 0, "SYS$HELP:HOLOTAPE.HLP" },
    { "MAIL", 0, 0, NULL, 0, NULL, N(mail_keys), mail_keys, "Action", 0, "SYS$HELP:MAIL.HLP" },
    { "AUTHORIZE", 0, 0, NULL, 0, NULL, N(authorize_keys), authorize_keys, "Action", DCL_PRIV_SECURITY,
      "SYS$HELP:AUTHORIZ.HLP" },
};

const dcl_table_t dcl_uos_table = { sizeof(uos_verbs) / sizeof(uos_verbs[0]), uos_verbs };
