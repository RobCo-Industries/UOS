/* DCL message catalogue: the exact text of every message and the formatter's bounds. */

#include <stdio.h>
#include <string.h>

#include "dcl.h"
#include "rc_test.h"

static const struct {
    dcl_msg_t id;
    const char *line;
} expect[] = {
    { DCL_NORMAL, "%DCL-S-NORMAL, normal successful completion" },
    { DCL_CONTIN, "%DCL-I-CONTIN, command continues on the next line" },
    { DCL_IVVERB, "%DCL-W-IVVERB, unrecognized command verb - check validity and spelling" },
    { DCL_ABVERB, "%DCL-W-ABVERB, ambiguous command verb - supply more characters" },
    { DCL_IVKEYW, "%DCL-W-IVKEYW, unrecognized keyword - check validity and spelling" },
    { DCL_ABKEYW, "%DCL-W-ABKEYW, ambiguous keyword - supply more characters" },
    { DCL_IVQUAL, "%DCL-W-IVQUAL, unrecognized qualifier" },
    { DCL_ABQUAL, "%DCL-W-ABQUAL, ambiguous qualifier - supply more characters" },
    { DCL_NONEG, "%DCL-W-NONEG, qualifier cannot be negated" },
    { DCL_INSFPRM, "%DCL-W-INSFPRM, missing command parameters - supply all required parameters" },
    { DCL_MAXPARM, "%DCL-W-MAXPARM, too many parameters" },
    { DCL_MAXQUAL, "%DCL-W-MAXQUAL, too many qualifiers" },
    { DCL_VALREQ, "%DCL-W-VALREQ, missing qualifier or keyword value" },
    { DCL_NOVALU, "%DCL-W-NOVALU, value not allowed" },
    { DCL_NOLIST, "%DCL-W-NOLIST, list of values not allowed" },
    { DCL_MAXVAL, "%DCL-W-MAXVAL, too many values in list" },
    { DCL_IVNUMB, "%DCL-W-IVNUMB, invalid number" },
    { DCL_NUMRNG, "%DCL-W-NUMRNG, number out of range" },
    { DCL_UNTERM, "%DCL-W-UNTERM, unterminated quoted string" },
    { DCL_PARENS, "%DCL-W-PARENS, missing closing parenthesis" },
    { DCL_IVCHAR, "%DCL-W-IVCHAR, invalid character in command" },
    { DCL_LINOVF, "%DCL-W-LINOVF, command line too long" },
    { DCL_TKNOVF, "%DCL-W-TKNOVF, command element too long" },
    { DCL_SYS_NOPRIV, "%UOS-E-NOPRIV, not enough privilege for this operation" },
    { DCL_SYS_FNF, "%UOS-E-FNF, file not found" },
    { DCL_SYS_DNF, "%UOS-E-DNF, directory not found" },
    { DCL_SYS_NOFILES, "%UOS-W-NOFILES, no files found" },
    { DCL_SYS_EXISTS, "%UOS-E-EXISTS, file already exists" },
    { DCL_SYS_IVSPEC, "%UOS-E-IVSPEC, invalid file specification" },
    { DCL_SYS_READERR, "%UOS-E-READERR, error reading file" },
    { DCL_SYS_WRITERR, "%UOS-E-WRITERR, error writing file" },
    { DCL_SYS_DISKFULL, "%UOS-E-DISKFULL, no space left on the volume" },
    { DCL_SYS_NODEV, "%UOS-E-NODEV, no such device" },
    { DCL_SYS_NOUSER, "%UOS-E-NOUSER, no such user" },
    { DCL_SYS_CANCEL, "%UOS-I-CANCEL, operation cancelled" },
    { DCL_SYS_COPIED, "%COPY-S-COPIED, 0 files copied" },
    { DCL_SYS_DELETED, "%DELETE-S-DELETED, 0 files deleted" },
    { DCL_SYS_RENAMED, "%RENAME-S-RENAMED, 0 files renamed" },
    { DCL_SYS_CREATED, "%CREATE-S-CREATED,  created" },
};

TEST(every_message_text)
{
    char buf[DCL_MSG_SIZE];
    size_t i;

    CHECK_EQ(sizeof(expect) / sizeof(expect[0]), DCL_MSG_COUNT);
    for (i = 0; i < sizeof(expect) / sizeof(expect[0]); i++) {
        CHECK_EQ(expect[i].id, i);
        CHECK_EQ(dcl_format_message(buf, sizeof(buf), expect[i].id, NULL, 0),
                 strlen(expect[i].line));
        CHECK_STR(buf, expect[i].line);
    }
}

TEST(parts)
{
    CHECK_STR(dcl_msg_facility(DCL_IVVERB), "DCL");
    CHECK_EQ(dcl_msg_severity(DCL_IVVERB), 'W');
    CHECK_STR(dcl_msg_ident(DCL_IVVERB), "IVVERB");
    CHECK_STR(dcl_msg_text(DCL_SYS_COPIED), "!N file!P copied");
    CHECK(dcl_msg_facility(DCL_MSG_COUNT) == NULL);
    CHECK_EQ(dcl_msg_severity(DCL_MSG_COUNT), 0);
    CHECK(dcl_msg_ident(DCL_MSG_COUNT) == NULL);
    CHECK(dcl_msg_text(DCL_MSG_COUNT) == NULL);
}

TEST(severities_are_valid)
{
    int i;

    for (i = 0; i < DCL_MSG_COUNT; i++) {
        CHECK(strchr("SIWEF", dcl_msg_severity((dcl_msg_t)i)) != NULL);
    }
}

TEST(token_line)
{
    char buf[DCL_MSG_SIZE];

    dcl_format_message(buf, sizeof(buf), DCL_IVVERB, "FOO", 0);
    CHECK_STR(buf, "%DCL-W-IVVERB, unrecognized command verb - check validity and spelling\n \\FOO\\");
    dcl_format_message(buf, sizeof(buf), DCL_SYS_FNF, "SD0:[HOME]A.TXT", 0);
    CHECK_STR(buf, "%UOS-E-FNF, file not found\n \\SD0:[HOME]A.TXT\\");
}

TEST(counts_and_names)
{
    char buf[DCL_MSG_SIZE];

    dcl_format_message(buf, sizeof(buf), DCL_SYS_COPIED, NULL, 1);
    CHECK_STR(buf, "%COPY-S-COPIED, 1 file copied");
    dcl_format_message(buf, sizeof(buf), DCL_SYS_DELETED, NULL, 2);
    CHECK_STR(buf, "%DELETE-S-DELETED, 2 files deleted");
    dcl_format_message(buf, sizeof(buf), DCL_SYS_RENAMED, NULL, 4294967295u);
    CHECK_STR(buf, "%RENAME-S-RENAMED, 4294967295 files renamed");
    dcl_format_message(buf, sizeof(buf), DCL_SYS_CREATED, "NOTES.TXT", 0);
    CHECK_STR(buf, "%CREATE-S-CREATED, NOTES.TXT created");
}

TEST(buffer_bounds)
{
    char buf[DCL_MSG_SIZE];
    const char *full = "%DCL-W-MAXPARM, too many parameters";
    size_t n = strlen(full);
    char token[DCL_TOKEN_MAX + 1];
    int i;

    CHECK_EQ(dcl_format_message(buf, n + 1, DCL_MAXPARM, NULL, 0), n);
    CHECK_STR(buf, full);
    memset(buf, 'x', sizeof(buf));
    CHECK_EQ(dcl_format_message(buf, n, DCL_MAXPARM, NULL, 0), -1);
    CHECK_STR(buf, "");
    CHECK_EQ(dcl_format_message(buf, 1, DCL_MAXPARM, NULL, 0), -1);
    CHECK_EQ(dcl_format_message(buf, 0, DCL_MAXPARM, NULL, 0), -1);
    CHECK_EQ(dcl_format_message(NULL, 10, DCL_MAXPARM, NULL, 0), -1);
    CHECK_EQ(dcl_format_message(buf, sizeof(buf), DCL_MSG_COUNT, NULL, 0), -1);
    CHECK_STR(buf, "");
    /* DCL_MSG_SIZE holds every message with the longest token. */
    memset(token, 'T', DCL_TOKEN_MAX);
    token[DCL_TOKEN_MAX] = '\0';
    for (i = 0; i < DCL_MSG_COUNT; i++) {
        CHECK(dcl_format_message(buf, sizeof(buf), (dcl_msg_t)i, token, 4294967295u) > 0);
    }
}
