/* Host tests of src/kernel/settings.c: the UOS.CFG reader. */

#include "rc_test.h"

#include "uos_settings.h"

static int parse(const char *text, uos_settings_t *s, uos_settings_report_t *r)
{
    uos_settings_default(s);
    return uos_settings_parse(text, strlen(text), s, r);
}

TEST(defaults)
{
    uos_settings_t s;

    uos_settings_default(&s);
    CHECK_STR(s.name, "ROBCO TERMINAL");
    CHECK_EQ(s.scheme, UOS_SCHEME_UNSET);
    CHECK_EQ(s.mode, UOS_MODE_SHELL);
    CHECK_EQ(s.login_retries, 3u);
    CHECK_EQ(s.login_lockout_s, 30u);
    CHECK_EQ(s.hack_enabled, 1);
    CHECK_STR(s.hack_user, "GUEST");
    CHECK_EQ(s.hack_attempts, 4u);
    CHECK_EQ(s.hack_difficulty, UOS_DIFF_ADVANCED);
    CHECK_EQ(s.hack_lockout_s, 60u);
}

TEST(empty_file_keeps_defaults)
{
    uos_settings_t s;
    uos_settings_report_t r;

    CHECK_EQ(parse("", &s, &r), UOS_CFG_OK);
    CHECK_EQ(r.keys, 0u);
    CHECK_STR(s.name, "ROBCO TERMINAL");
}

TEST(every_key)
{
    uos_settings_t s;
    uos_settings_report_t r;
    const char *cfg =
        "[system]\nname = Vault 7 Overseer\nscheme = AMBER\nmode = terminal\n"
        "[login]\nretries = 5\nlockout_s = 120\n"
        "[hack]\nenabled = no\nuser = oper\nattempts = 2\ndifficulty = master\nlockout_s = 0\n";

    CHECK_EQ(parse(cfg, &s, &r), UOS_CFG_OK);
    CHECK_EQ(r.keys, 10u);
    CHECK_EQ(r.bad + r.clamped + r.unknown, 0u);
    CHECK_STR(s.name, "Vault 7 Overseer");
    CHECK_EQ(s.scheme, 1);
    CHECK_EQ(s.mode, UOS_MODE_TERMINAL);
    CHECK_EQ(s.login_retries, 5u);
    CHECK_EQ(s.login_lockout_s, 120u);
    CHECK_EQ(s.hack_enabled, 0);
    CHECK_STR(s.hack_user, "OPER");
    CHECK_EQ(s.hack_attempts, 2u);
    CHECK_EQ(s.hack_difficulty, UOS_DIFF_MASTER);
    CHECK_EQ(s.hack_lockout_s, 0u);
}

TEST(clamps_to_the_limits)
{
    uos_settings_t s;
    uos_settings_report_t r;

    CHECK_EQ(parse("[login]\nretries = 0\nlockout_s = 3601\n"
                   "[hack]\nattempts = 10\nlockout_s = 99999999999\n", &s, &r), UOS_CFG_OK);
    CHECK_EQ(s.login_retries, 1u);
    CHECK_EQ(s.login_lockout_s, 3600u);
    CHECK_EQ(s.hack_attempts, 9u);
    CHECK_EQ(s.hack_lockout_s, 3600u);
    CHECK_EQ(r.clamped, 4u);
    CHECK_EQ(r.keys, 4u);
    CHECK_EQ(r.bad, 0u);
}

TEST(values_at_the_limits_are_not_clamped)
{
    uos_settings_t s;
    uos_settings_report_t r;

    CHECK_EQ(parse("[login]\nretries = 9\nlockout_s = 0\n[hack]\nattempts = 1\n"
                   "lockout_s = 3600\n", &s, &r), UOS_CFG_OK);
    CHECK_EQ(r.clamped, 0u);
    CHECK_EQ(s.login_retries, 9u);
    CHECK_EQ(s.login_lockout_s, 0u);
    CHECK_EQ(s.hack_attempts, 1u);
    CHECK_EQ(s.hack_lockout_s, 3600u);
}

TEST(bad_values_are_skipped_and_counted)
{
    uos_settings_t s;
    uos_settings_report_t r;

    CHECK_EQ(parse("[system]\nscheme = purple\nmode = gui\n"
                   "name = 12345678901234567890123456789012\n"
                   "[login]\nretries = three\nlockout_s = -5\n"
                   "[hack]\nenabled = maybe\nuser = no one\ndifficulty = easy\nattempts =\n",
                   &s, &r), UOS_CFG_OK);
    CHECK_EQ(r.bad, 9u);
    CHECK_EQ(r.keys, 0u);
    CHECK_EQ(s.scheme, UOS_SCHEME_UNSET);
    CHECK_STR(s.name, "ROBCO TERMINAL");
    CHECK_EQ(s.login_retries, 3u);
    CHECK_STR(s.hack_user, "GUEST");
}

TEST(name_length_limit)
{
    uos_settings_t s;
    uos_settings_report_t r;

    CHECK_EQ(parse("[system]\nname = 1234567890123456789012345678901\n", &s, &r), UOS_CFG_OK);
    CHECK_EQ(r.keys, 1u);
    CHECK_EQ(strlen(s.name), (size_t)UOS_NAME_MAX);
}

TEST(user_length_limit)
{
    uos_settings_t s;
    uos_settings_report_t r;

    CHECK_EQ(parse("[hack]\nuser = A23456789012345\n", &s, &r), UOS_CFG_OK);
    CHECK_STR(s.hack_user, "A23456789012345");
    CHECK_EQ(parse("[hack]\nuser = A234567890123456\n", &s, &r), UOS_CFG_OK);
    CHECK_EQ(r.bad, 1u);
    CHECK_STR(s.hack_user, "GUEST");
}

TEST(unknown_keys_and_sections)
{
    uos_settings_t s;
    uos_settings_report_t r;

    CHECK_EQ(parse("top = 1\n[system]\ncolour = green\n[net]\nhost = x\n[hack]\nwords = 9\n",
                   &s, &r), UOS_CFG_OK);
    CHECK_EQ(r.unknown, 4u);
    CHECK_EQ(r.keys, 0u);
}

TEST(last_key_wins)
{
    uos_settings_t s;
    uos_settings_report_t r;

    CHECK_EQ(parse("[system]\nscheme = blue\nscheme = white\n", &s, &r), UOS_CFG_OK);
    CHECK_EQ(s.scheme, 3);
}

TEST(syntax_error_keeps_the_keys_before_it)
{
    uos_settings_t s;
    uos_settings_report_t r;

    CHECK_EQ(parse("[system]\nscheme = blue\nthis is not ini\nname = LATE\n", &s, &r),
             UOS_CFG_SYNTAX);
    CHECK_EQ(r.err_line, 3u);
    CHECK_EQ(s.scheme, 2);
    CHECK_STR(s.name, "ROBCO TERMINAL");
}

TEST(numbers_with_leading_zeros)
{
    uos_settings_t s;
    uos_settings_report_t r;

    CHECK_EQ(parse("[login]\nlockout_s = 000000000000045\n", &s, &r), UOS_CFG_OK);
    CHECK_EQ(s.login_lockout_s, 45u);
    CHECK_EQ(r.clamped, 0u);
}

TEST(bad_arguments)
{
    uos_settings_t s;
    uos_settings_report_t r;

    uos_settings_default(&s);
    CHECK_EQ(uos_settings_parse(NULL, 4, &s, &r), UOS_CFG_ARG);
    CHECK_EQ(uos_settings_parse("", 0, NULL, &r), UOS_CFG_ARG);
    CHECK_EQ(uos_settings_parse("", 0, &s, NULL), UOS_CFG_ARG);
    CHECK_EQ(uos_settings_parse(NULL, 0, &s, &r), UOS_CFG_OK);
}

TEST(names)
{
    CHECK_STR(uos_scheme_name(0), "green");
    CHECK_STR(uos_scheme_name(1), "amber");
    CHECK_STR(uos_scheme_name(2), "blue");
    CHECK_STR(uos_scheme_name(3), "white");
    CHECK_STR(uos_scheme_name(UOS_SCHEME_UNSET), "default");
    CHECK_STR(uos_scheme_name(4), "unknown");
    CHECK_STR(uos_mode_name(UOS_MODE_SHELL), "shell");
    CHECK_STR(uos_mode_name(UOS_MODE_TERMINAL), "terminal");
    CHECK_STR(uos_mode_name(2), "unknown");
    CHECK_STR(uos_difficulty_name(UOS_DIFF_NOVICE), "novice");
    CHECK_STR(uos_difficulty_name(UOS_DIFF_EXPERT), "expert");
    CHECK_STR(uos_difficulty_name(-1), "unknown");
}
