//----------------------------------------------------------------------------//
// -*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-  //
//                                                                            //
// batteryinfo - a simple battery status and information tool for Linux       //
// systems.                                                                   //
//                                                                            //
// Compile with something like:                                               //
//     gcc batteryinfo.c -o batteryinfo.o -O3 -Wall                           //
//                                                                            //
// The program gets its data from /sys/class/power_supply, looking for        //
// directories that contain a file called "type" which contains the single    //
// word "Battery". If found, it then uses the "uevent" file, which should be  //
// located alongside the "type" file.                                         //
// The information provided by the uevent file varies from system to system,  //
// so some systems may not be able to provide certain pieces of information   //
// (such as temperature).                                                     //
//                                                                            //
// *-,_,-*'^^'*-,_,-*'^^'*-,_,-*'^^'*-,_,-*'^^'*-,_,-*'^^'*-,_,-*'^^'*-,_,-*  //
//                                                                            //
// Copyright (c) 2016 Joe Glancy.                                             //
//                                                                            //
// This program is free software: you can redistribute it and/or modify       //
// it under the terms of the GNU General Public License as published by       //
// the Free Software Foundation, either version 3 of the License, or          //
// (at your option) any later version.                                        //
//                                                                            //
// This program is distributed in the hope that it will be useful,            //
// but WITHOUT ANY WARRANTY; without even the implied warranty of             //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              //
// GNU General Public License for more details.                               //
//                                                                            //
// You should have received a copy of the GNU General Public License          //
// along with this program.  If not, see <http://www.gnu.org/licenses/>.      //
//                                                                            //
//----------------------------------------------------------------------------//

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <float.h>
#define _GNU_SOURCE
#include <getopt.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// comment the line below if you don't want color output
#define COLOR_OUTPUT                    1

#ifdef COLOR_OUTPUT
#       define FMT_RESET                "\033[0m"
#       define FMT_RED                  "\033[1;31m"
#       define FMT_GREEN                "\033[1;32m"
#       define FMT_YELLOW               "\033[1;33m"
#else
#       define FMT_RESET
#       define FMT_RED
#       define FMT_GREEN
#       define FMT_YELLOW
#endif

#define PROGRAM_NAME                            "batteryinfo" ///< Program name.
#define PROGRAM_VERSION_STR                     "1.2.1" ///< Program version string.

#define SYS_FS_BATTERY_BASE_PATH                "/sys/class/power_supply/"
#define SYS_FS_BATTERY_BASE_PATH_LEN            sizeof(SYS_FS_BATTERY_BASE_PATH)

#define DEFAULT_OUTPUT_SEQUENCE                 "ncvCmMedsp" ///< The default output sequence for battery information.
#define COMPLETE_OUTPUT_SEQUENCE                "nctvCTdmMeshSHrpogD" ///< The complete output sequence for all battery information.

#define CONFIG_FLAG_DIGITS                      0x00001 ///< Digit output for flags (1/0 instead of yes/no, or true/false in JSON's case) config flag.
#define CONFIG_FLAG_BY_NAME                     0x00002 ///< Output info for named battery config flag.
#define CONFIG_FLAG_OUTPUT_ALL                  0x00004 ///< Output every possible piece of information.
#define CONFIG_FLAG_DISABLE_CHARGE_CAP          0x00008 ///< Disable the 100% charge capacity cap.

#define DOUBLE_INVALID                          DBL_MIN  ///< Invalid double value. Assume that this will never be read as an actual value, so that it can represent an invalid one.
#define LONG_INVALID                            LONG_MIN ///< Invalid long value.  Assume that this will also never be read as an actual value, so that it can represent an invalid one.

#define free_if_not_null(p) if (p != NULL) free((void*) p) ///< Macro to free the memory address pointed to by p if it's value is not NULL.

#define error(a, b...) fprintf(stderr, FMT_RED "error" FMT_RESET ": " a, ##b)

//------------------------------------------------------------------------------
// _,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,_
//------------------------------------------------------------------------------

/** Short usage information string. */
static const char short_usage_str[] =
        "Usage: " PROGRAM_NAME " <output sequence>\n"
        "           [-h | --help] [-v | --version] [-l | --license]\n"
        "           [-a | --all] [-d | --digits] [-n | --name <name>] [-j | --json]\n"
        "Use `" PROGRAM_NAME " -h' for more information.\n";

/** Longer, more extensive usage information string. */
static const char usage_str[] =
        "Usage: " PROGRAM_NAME " <output sequence>\n"
        "           [-h | --help] [-v | --version] [-l | --license]\n"
        "           [-a | --all] [-d | --digits] [-n | --name <name>] [-j | --json]\n"
        "\n"
        "`output sequence' is a sequence of the below characters, in any order, which\n"
        "determines what information is listed about available batteries.\n"
        "The order in which they are displayed in will be the order in which the\n"
        "characters are given in the argument.\n"
        "    n           battery name\n"
        "    c           current charge, in %\n" // single %s are fine as we're using fputs, not fprintf
        "    t           the maximum capacity of charge which the battery\n"
        "                can hold (in %), relative to what it was designed\n"
        "                to hold.\n"
        "    v           current voltage, in V\n"
        "    C           current current, in A\n"
        "    T           current temperature, in deg. C\n"
        "    d           battery driver\n"
        "    m           battery model\n"
        "    M           battery manufacturer\n"
        "    e           battery technology\n"
        "    s           current battery status\n"
        "    h           battery health\n"
        "    S           battery serial number\n"
        "    H           battery charge type\n"
        "    r           battery charge rate\n"
        "    p           whether the battery is present or not\n"
        "    o           whether the battery is online or not\n"
        "    g           whether charging is enabled for this battery or not\n"
        "    D           estimated time until the battery is completely\n"
        "                discharged (remaining battery life), in hours.\n"
        "                This assumes that the current battery drain will\n"
        "                remain constant.\n"
        "If the output sequence is not provided, it will default to:\n"
        "        " DEFAULT_OUTPUT_SEQUENCE "\n"
        "If there is no data available for one of the above mentioned parameters, a\n"
        "question mark (\"?\") is outputted instead, if the output format is CSV. If\n"
        "the output format is JSON, a null value will be used to indicate the absence\n"
        "of a certain piece of data.\n"
        "\n"
        "Options:\n"
        "   -h,--help         display this help text.\n"
        "   -v,--version      display the program's version.\n"
        "   -l,--license      display this program's copyright and licensing\n"
        "                     information.\n"
        "   -a,--all          display every possible piece of data (i.e: essentially\n"
        "                     filling `output sequence' with every valid character).\n"
        "   -d,--digits       instead of outputting \"yes\" and \"no\" (or \"true\" and\n"
        "                     \"false\" for JSON output) for flags, use \"1\" and \"0\".\n"
        "   -n,--name <name>  specify the name of a battery to output information for.\n"
        "                     If no battery by that name is found, the output will be\n"
        "                     empty (unless the output format is in JSON, in which case\n"
        "                     the `batteries' array will be empty).\n"
        "   -j,--json         output battery information in JSON format.\n";

/** License string. */
static const char license_str[] =
        "Copyright (c) 2016 Joe Glancy\n"
        "\n"
        "This program is free software: you can redistribute it and/or modify\n"
        "it under the terms of the GNU General Public License as published by\n"
        "the Free Software Foundation, either version 3 of the License, or\n"
        "(at your option) any later version.\n"
        "\n"
        "This program is distributed in the hope that it will be useful,\n"
        "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
        "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
        "GNU General Public License for more details.\n"
        "\n"
        "You should have received a copy of the GNU General Public License\n"
        "along with this program.  If not, see <http://www.gnu.org/licenses/>.\n";

/** Version string. */
static const char version_str[] = PROGRAM_NAME" v"PROGRAM_VERSION_STR"\n";

/** Output format enumerations. */
enum {
        OUTPUT_FORMAT_CSV,
        OUTPUT_FORMAT_JSON
};

/** Long argument definitions for getopt_long. */
static const struct option long_command_line_opts[] = {
        { "help", no_argument, NULL, 'h' },
        { "version", no_argument, NULL, 'v' },
        { "license", no_argument, NULL, 'l' },
        { "all", no_argument, NULL, 'a' },
        { "digits", no_argument, NULL, 'd' },
        { "json", no_argument, NULL, 'j' },
        { "name", required_argument, NULL, 'n' },
        { "no-cap", no_argument, NULL, 'N'},
        { NULL, 0, NULL, 0 }
};

//------------------------------------------------------------------------------
// _,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,_
//------------------------------------------------------------------------------

/** Structure to hold various program configuration parameters. */
struct config {
        uint64_t configflags;   ///< Configuration flags.
        int output_format;      ///< Output format.
        struct {
                char *n;        ///< The value of the -n,--name option, if it was provided on the command line.
        } cmdopts;              ///< Struct to store the values for options on the command line that take them.
};

/** Structure to hold information about a specific battery. */
struct battery_info {
        double charge;         ///< Current battery charge (0-100%).
        double max_charge;     ///< Maximum possible battery charge (usually less than 100% because of general battery degredation) (0-100%).
        double voltage;        ///< Current battery voltage.
        double current;        ///< Current battery current.
        double temperature;    ///< Current battery temperature.
        double etd;            ///< Estimated Time until Discharge, i.e: the (estimated) amount of time left until the battery is completely discharged.

        char *name;            ///< Battery name (as per what the system gave it).
        char *model;           ///< Battery model.
        char *manufacturer;    ///< Battery manufacturer.
        char *technology;      ///< Battery technology.
        char *driver;          ///< Battery driver.
        char *status;          ///< Current battery status.
        char *health;          ///< Current battery health.
        char *serial_number;   ///< Battery serial number.
        char *charge_type;     ///< Battery charge type.
        char *charge_rate;     ///< Battery charge rate.

        char present;          ///< Is the battery present?
        char online;           ///< Is the battery online?
        char charging_enabled; ///< Does the battery have charging enabled?
};

//------------------------------------------------------------------------------
// _,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,_
//------------------------------------------------------------------------------

/** Routine to initialize a config structure with blank values.
 * \param config A pointer to the structure to initialize.
 */
static void config_init(struct config *config)
{
        config->configflags = 0;
        config->output_format = OUTPUT_FORMAT_CSV;
        config->cmdopts.n = NULL;
}

/** Routine to initialize a battery_info structure with blank values.
 * \param info A pointer to the structure to initialize.
 */
static void
battery_info_init(struct battery_info *info)
{
        info->charge = DOUBLE_INVALID;
        info->max_charge = DOUBLE_INVALID;
        info->voltage = DOUBLE_INVALID;
        info->current = DOUBLE_INVALID;
        info->temperature = DOUBLE_INVALID;
        info->etd = DOUBLE_INVALID;

        info->name = NULL;
        info->model = NULL;
        info->manufacturer = NULL;
        info->technology = NULL;
        info->status = NULL;
        info->health = NULL;
        info->serial_number = NULL;
        info->charge_type = NULL;
        info->charge_rate = NULL;
        info->driver = NULL;

        info->present = -1;
        info->online = -1;
        info->charging_enabled = -1;
}

/** Routine to clean up a battery_info structure by freeing any allocated memory.
 * \param info A pointer to the structure to clean up.
 */
static void
battery_info_cleanup(struct battery_info *info)
{
        free_if_not_null(info->name);
        free_if_not_null(info->model);
        free_if_not_null(info->manufacturer);
        free_if_not_null(info->technology);
        free_if_not_null(info->driver);
        free_if_not_null(info->status);
        free_if_not_null(info->health);
        free_if_not_null(info->serial_number);
        free_if_not_null(info->charge_type);
        free_if_not_null(info->charge_rate);
}

//------------------------------------------------------------------------------
// _,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,_
//------------------------------------------------------------------------------

#define output_n_spaces(n) do { \
                size_t i = n; \
                while (i-- > 0) fputc(' ', stdout); \
        } while (0)
#define output_csv_name() do { \
                fputs(name, stdout); \
                fputc(':', stdout); \
                output_n_spaces(29 - strlen(name)); \
        } while (0)

/** Output routine for the beginning of outputting all battery information.
 * \param config A pointer to the program configuration struct.
 */
static void
battery_info_output_init(struct config *config)
{
        switch (config->output_format) {
                case OUTPUT_FORMAT_CSV: {
                        break;
                }
                case OUTPUT_FORMAT_JSON: {
                        fputs("{\n\"batteries\": [\n", stdout);
                        break;
                }
                default:
                        break;
        }
}

/** Output routine for the end of outputting all battery information.
 * \param config A pointer to the program configuration struct.
 */
static void
battery_info_output_deinit(struct config *config)
{
        switch (config->output_format) {
                case OUTPUT_FORMAT_CSV: {
                        break;
                }
                case OUTPUT_FORMAT_JSON: {
                        fputs("]\n}\n", stdout);
                        break;
                }
                default:
                        break;
        }
}

/** Output routine for the beginning of outputting invividual battery
 * information.
 * \param battery The current battery.
 * \param config A pointer to the program configuration struct.
 */
static void
battery_info_output_start(int battery,
                          struct config *config)
{
        switch (config->output_format) {
                case OUTPUT_FORMAT_CSV: {
                        printf("battery:                      %d\n", battery);
                        break;
                }
                case OUTPUT_FORMAT_JSON: {
                        printf("\t{\n\t\t\"battery\": %d", battery);
                        break;
                }
                default:
                        break;
        }
}

/** Output routine for the end of outputting invividual battery information.
 * \param config A pointer to the program configuration struct.
 */
static void
battery_info_output_end(struct config *config)
{
        switch (config->output_format) {
                case OUTPUT_FORMAT_CSV: {
                        break;
                }
                case OUTPUT_FORMAT_JSON: {
                        fputs("\n\t},\n", stdout);
                        break;
                }
                default:
                        break;
        }
}

/** Output routine for outputting a double value in the correct format.
 * \param d The value.
 * \param name The value's identifiable name.
 * \param config A pointer to the program configuration struct.
 */
static void
battery_info_output_double(double d,
                           const char *name,
                           struct config *config)
{
        switch (config->output_format) {
                case OUTPUT_FORMAT_CSV: {
                        output_csv_name();
                        if (d != DOUBLE_INVALID) {
                                printf("%.2f\n", d);
                        } else {
                                fputs("?\n", stdout);
                        }
                        break;
                }
                case OUTPUT_FORMAT_JSON: {
                        printf(",\n\t\t\"%s\": ", name);
                        if (d != DOUBLE_INVALID) {
                                printf("%.2f", d);
                        } else {
                                fputs("null", stdout);
                        }
                        break;
                }
                default:
                        break;
        }
}

/** Output routine for outputting a double value in the correct format, as a
 * percentage.
 * \param d The value.
 * \param name The value's identifiable name.
 * \param config A pointer to the program configuration struct.
 */
static void
battery_info_output_double_percent(double d,
                                   const char *name,
                                   struct config *config)
{
        switch (config->output_format) {
                case OUTPUT_FORMAT_CSV: {
                        output_csv_name();
                        if (d != DOUBLE_INVALID) {
                                printf("%.2f%%\n", d);
                        } else {
                                fputs("?\n", stdout);
                        }
                        break;
                }
                case OUTPUT_FORMAT_JSON: {
                        // we don't want % signs in the JSON
                        printf(",\n\t\t\"%s\": ", name);
                        if (d != DOUBLE_INVALID) {
                                printf("%.2f", d);
                        } else {
                                fputs("null", stdout);
                        }
                        break;
                }
                default:
                        break;
        }
}

/** Output routine for outputting a string in the correct format.
 * \param s The string.
 * \param name The string's identifiable name.
 * \param config A pointer to the program configuration struct.
 */
static void
battery_info_output_str(const char *s,
                        const char *name,
                        struct config *config)
{
        switch (config->output_format) {
                case OUTPUT_FORMAT_CSV: {
                        output_csv_name();
                        if (s == NULL) {
                                fputs("?\n", stdout);
                        } else {
                                printf("%s\n", s);
                        }
                        break;
                }
                case OUTPUT_FORMAT_JSON: {
                        printf(",\n\t\t\"%s\": ", name);
                        if (s == NULL) {
                                fputs("null", stdout);
                        } else {
                                printf("\"%s\"", s);
                        }
                        break;
                }
                default:
                        break;
        }
}

/** Output routine for outputting a true/false flag in the correct format.
 * \param flag The flag value.
 * \param name The flag value's identifiable name.
 * \param config A pointer to the program configuration struct.
 */
static void
battery_info_output_flag(int flag,
                         const char *name,
                         struct config *config)
{
        switch (config->output_format) {
                case OUTPUT_FORMAT_CSV: {
                        output_csv_name();
                        if (config->configflags & CONFIG_FLAG_DIGITS) {
                                printf("%s\n", flag == 1 ? "1" :
                                                        flag == 0 ? "0" : "?");
                        } else {
                                printf("%s\n", flag == 1 ? "yes" :
                                                        flag == 0 ? "no" : "?");
                        }
                        break;
                }
                case OUTPUT_FORMAT_JSON: {
                        printf(",\n\t\t\"%s\": ", name);
                        if (config->configflags & CONFIG_FLAG_DIGITS) {
                                if (flag == 0) {
                                        fputc('0', stdout);
                                } else if (flag == 1) {
                                        fputc('1', stdout);
                                } else {
                                        fputs("null", stdout);
                                }
                        } else {
                                if (flag == 0) {
                                        fputs("false", stdout);
                                } else if (flag == 1) {
                                        fputs("true", stdout);
                                } else {
                                        fputs("null", stdout);
                                }
                        }
                        break;
                }
                default:
                        break;
        }
}

#undef output_n_spaces
#undef output_csv_name

//------------------------------------------------------------------------------
// _,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,_
//------------------------------------------------------------------------------

/** Utility routine for converting a string into an integer (strtol wrapper).
 * \param s The string to convert.
 * \param dest A pointer to the long integer to place the result in.
 * \return 0 on success, -1 on error.
 */
static int
strtol_helper(char *s,
              long *dest)
{
        errno = 0;
        char *endptr;

        *dest = strtol(s, &endptr, 10);

        if (errno != 0 || endptr == s || (endptr != NULL && *endptr != '\0')) {
                *dest = LONG_INVALID;
                return -1;
        }

        return 0;
}

/** Utility routine for copying strings by allocating memory for a copy and then
 * copying the source to it.
 * \param src The source string to copy.
 * \param dest A pointer to a char pointer in which to place a pointer to the
 * resulting allocated string.
 * \return 0 on success, -1 on error.
 */
static int
strcpy_helper(char *src,
              char **dest)
{
        free_if_not_null(*dest);
        *dest = (char*) malloc(strlen((const char*) src) + 1);
        if (*dest == NULL) {
                return -1;
        }

        strcpy(*dest, (const char*) src);
        return 0;
}

/** Utility routine for comparing a file's contents to a string.
 * \param path The path of the file to compare.
 * \param comparison The string to compare the file's contents to.
 * \return 0 on success, -1 on error or when the file doesn't match the given
 * string.
 */
static int
compare_file_contents(const char *path,
                      const char *comparison)
{
        FILE *f = fopen(path, "r");
        if (f == NULL) {
                return -1;
        }

        size_t len = strlen(comparison);
        char *s = (char*) comparison;
        int c;

        while (len > 0) {
                c = fgetc(f);
                if (c == EOF || c != (int) *s) {
                        break;
                }
                len--;
                s++;
        }

        fclose(f);

        return len == 0 ? 0 : -1;
}

//------------------------------------------------------------------------------
// _,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,_
//------------------------------------------------------------------------------

#define if_startswith(s) if (strstr((const char*) buf, s) == buf)

/** Routine to read a battery entry's uevent file, and place the parsed data
 * into a battery_info structure.
 * \param path The path to the battery's directory entry in the sysfs.
 * \param info A pointer to the structure in which to place the parsed data.
 * \return 0 on success, -1 on error. NOTE:

 */
static int
get_battery_info(const char *path,
                 struct battery_info *info,
                 struct config *config)
{
        long charge_now = LONG_INVALID,
             charge_full = LONG_INVALID,
             charge_full_design = LONG_INVALID,
             capacity = LONG_INVALID,
             voltage_now = LONG_INVALID,
             current_now = LONG_INVALID,
             temp = LONG_INVALID,
             online = LONG_INVALID,
             present = LONG_INVALID,
             charging_enabled = LONG_INVALID;

        char buf[256], pathbuf[SYS_FS_BATTERY_BASE_PATH_LEN + 64];
        strcpy((char*) pathbuf, path);
        strcat((char*) pathbuf, "/uevent");

        int failed_opens = 0;
        FILE *f = fopen(pathbuf, "r");
        if (f == NULL) {
                failed_opens++;
                goto read_device_uevent;
        }

        while (fgets(buf, sizeof(buf), f) != NULL) {
                // strip any whitespace at the end
                size_t i = strlen((const char*) buf) - 1;
                while (i > 0 && isspace((int) buf[i])) i--;

                if (i == 0) {
                        continue;
                }

                buf[i + 1] = '\0';

                // checking whether strtol_helper and strcpy_helper return >= 0 isn't really
                // necessary, but implemented in case some code is added below the large
                // if_startswith block which should be skipped if an error occurs with one
                // of the _helper routines.
                if_startswith("POWER_SUPPLY_CAPACITY") {
                        // some systems provide the capacity field, others don't. if they do, then the
                        // value can be directly used as the battery charge percentage. otherwise, work
                        // it out from the charge_now and charge_full values.
                        if (strtol_helper(buf + 22, &capacity) < 0) { continue; }
                } else if_startswith("POWER_SUPPLY_CHARGE_NOW") {
                        if (strtol_helper(buf + 24, &charge_now) < 0) { continue; }
                } else if_startswith("POWER_SUPPLY_CHARGE_FULL_DESIGN") {
                        // check for this one first, otherwise POWER_SUPPLY_CHARGE_FULL will be matched
                        if (strtol_helper(buf + 32, &charge_full_design) < 0) { continue; }
                } else if_startswith("POWER_SUPPLY_CHARGE_FULL") {
                        if (strtol_helper(buf + 25, &charge_full) < 0) { continue; }
                } else if_startswith("POWER_SUPPLY_VOLTAGE_NOW") {
                        if (strtol_helper(buf + 25, &voltage_now) < 0) { continue; }
                } else if_startswith("POWER_SUPPLY_CURRENT_NOW") {
                        if (strtol_helper(buf + 25, &current_now) < 0) { continue; }
                } else if_startswith("POWER_SUPPLY_TEMP") {
                        if (strtol_helper(buf + 18, &temp) < 0) { continue; }
                } else if_startswith("POWER_SUPPLY_NAME") {
                        if (strcpy_helper(buf + 18, &info->name) < 0) { continue; }
                } else if_startswith("POWER_SUPPLY_MODEL_NAME") {
                        if (strcpy_helper(buf + 24, &info->model) < 0) { continue; }
                } else if_startswith("POWER_SUPPLY_MANUFACTURER") {
                        if (strcpy_helper(buf + 26, &info->manufacturer) < 0) { continue; }
                } else if_startswith("POWER_SUPPLY_TECHNOLOGY") {
                        if (strcpy_helper(buf + 24, &info->technology) < 0) { continue; }
                } else if_startswith("POWER_SUPPLY_STATUS") {
                        if (strcpy_helper(buf + 20, &info->status) < 0) { continue; }
                } else if_startswith("POWER_SUPPLY_HEALTH") {
                        if (strcpy_helper(buf + 20, &info->health) < 0) { continue; }
                } else if_startswith("POWER_SUPPLY_SERIAL_NUMBER") {
                        if (strcpy_helper(buf + 27, &info->serial_number) < 0) { continue; }
                } else if_startswith("POWER_SUPPLY_CHARGE_TYPE") {
                        if (strcpy_helper(buf + 25, &info->charge_type) < 0) { continue; }
                } else if_startswith("POWER_SUPPLY_CHARGE_RATE") {
                        if(strcpy_helper(buf + 25, &info->charge_rate) < 0) { continue; }
                } else if_startswith("POWER_SUPPLY_PRESENT") {
                        if (strtol_helper(buf + 21, &present) < 0) { continue; }
                } else if_startswith("POWER_SUPPLY_ONLINE") {
                        if (strtol_helper(buf + 21, &online) < 0) { continue; }
                } else if_startswith("POWER_SUPPLY_CHARGING_ENABLED") {
                        if (strtol_helper(buf + 30, &charging_enabled) < 0) { continue; }
                }
        }

        fclose(f);

read_device_uevent:
        strcpy((char*) pathbuf, path);
        strcat((char*) pathbuf, "/device/uevent");
        if ((f = fopen(pathbuf, "r")) == NULL) {
                if (++failed_opens == 2) {
                        // skip processing anything if we couldn't open anything
                        goto end;
                }
                goto process;
        }

        while (fgets(buf, sizeof(buf), f) != NULL) {
                size_t i = strlen((const char*) buf) - 1;
                while (i > 0 && isspace((int) buf[i])) i--;

                if (i == 0) {
                        continue;
                }

                buf[i + 1] = '\0';

                if_startswith("DRIVER") {
                        if (strcpy_helper(buf + 7, &info->driver) < 0) { continue; }
                }
        }

        fclose(f);

process:
        if (capacity != LONG_INVALID && capacity >= 0 && capacity <= 100) {
                info->charge = (double) capacity;
        } else if (charge_now != LONG_INVALID && charge_full != LONG_INVALID) {
                info->charge = (double) charge_now / (double) charge_full * 100;
        }

        if (!(config->configflags & CONFIG_FLAG_DISABLE_CHARGE_CAP) &&
                info->charge > 100.0) {
                info->charge = 100.0;
        }

        if (charge_full != LONG_INVALID && charge_full_design != LONG_INVALID) {
                info->max_charge = (double) charge_full / (double) charge_full_design * 100;
        }

        // TODO fix: make sure this is right (the units)
        if (voltage_now != LONG_INVALID) {
                info->voltage = (double) voltage_now / (double) 1000000.0;
        }

        // TODO fix: ^
        if (current_now != LONG_INVALID) {
                info->current = (double) current_now / (double) 100000.0;
        }

        // TODO fix: ^
        if (temp != LONG_INVALID) {
                info->temperature = (double) temp / (double) 10.0;
        }

        if (charge_full != LONG_INVALID && charge_full != LONG_INVALID &&
                current_now != LONG_INVALID) {
                info->etd = (((double) charge_full - (double) charge_now) / (double) current_now) * 10;
        }

        if (present == 1 || present == 0) {
                info->present = (char) present;
        }
        if (online == 1 || online == 0) {
                info->online = (char) online;
        }
        if (charging_enabled == 1 || charging_enabled == 0) {
                info->charging_enabled = (char) charging_enabled;
        }

end:
        return failed_opens >= 2 ? -1 : 0; // if we manage to read at least something, count it as a success.
}

#undef if_startswith

/** Routine to get and list information about a specific battery, given a path
 * to its uevent file.
 * \param battery An index for the battery.
 * \param path The path of the battery's uevent file.
 * \param infostr The sequence of characters which denotes what information is
 * outputted, and its order.
 * \param configflags A set of flags which denote program-wide configuration
 * parameters.
 * \return 0 on success, -1 on failure.
 */
static int
list_battery_info(int battery,
                  const char *path,
                  char *infostr,
                  struct config *config)
{
        struct battery_info info;
        battery_info_init(&info);

        if (get_battery_info(path, &info, config) < 0) {
                return -1;
        }

        battery_info_output_start(battery, config);

        char *p = infostr;

        if (config->configflags & CONFIG_FLAG_OUTPUT_ALL) {
                p = COMPLETE_OUTPUT_SEQUENCE;
        }

        while (*p != '\0') {
                switch((int) *p) {
                        case 'n': {
                                battery_info_output_str(info.name, "name", config);
                                break;
                        }
                        case 'c': {
                                battery_info_output_double_percent(info.charge, "charge", config);
                                break;
                        }
                        case 't': {
                                battery_info_output_double_percent(info.max_charge, "max_charge", config);
                                break;
                        }
                        case 'v': {
                                battery_info_output_double(info.voltage, "voltage", config);
                                break;
                        }
                        case 'C': {
                                battery_info_output_double(info.current, "current", config);
                                break;
                        }
                        case 'T': {
                                battery_info_output_double(info.temperature, "temperature", config);
                                break;
                        }
                        case 'D': {
                                battery_info_output_double(info.etd, "etd", config);
                                break;
                        }
                        case 'd': {
                                battery_info_output_str(info.driver, "driver", config);
                                break;
                        }
                        case 'm': {
                                battery_info_output_str(info.model, "model", config);
                                break;
                        }
                        case 'M': {
                                battery_info_output_str(info.manufacturer, "manufacturer", config);
                                break;
                        }
                        case 'e': {
                                battery_info_output_str(info.technology, "technology", config);
                                break;
                        }
                        case 's': {
                                battery_info_output_str(info.status, "status", config);
                                break;
                        }
                        case 'h': {
                                battery_info_output_str(info.health, "health", config);
                                break;
                        }
                        case 'S': {
                                battery_info_output_str(info.serial_number, "serial_number", config);
                                break;
                        }
                        case 'H': {
                                battery_info_output_str(info.charge_type, "charge_type", config);
                                break;
                        }
                        case 'r': {
                                battery_info_output_str(info.charge_rate, "charge_rate", config);
                                break;
                        }
                        case 'p': {
                                battery_info_output_flag(info.present, "present", config);
                                break;
                        }
                        case 'o': {
                                battery_info_output_flag(info.online, "online", config);
                                break;
                        }
                        case 'g': {
                                battery_info_output_flag(info.charging_enabled, "charging_enabled", config);
                                break;
                        }
                        default: break;
                }
                p++;
        }

        battery_info_cleanup(&info);

        battery_info_output_end(config);

        return 0;
}

#define set_path_to_dir() do { \
                strcpy(path, sys_fs_path); \
                strcat(path, (const char*) dir->d_name); \
        } while (0)

/** Routine which goes through each entry in /sys/class/power_supply, checks
 * whether it's a battery, and then calls list_battery_info for each battery found.
 * \param infostr The sequence of characters which denotes what information is
 * outputted, and its order.
 * \param configflags A set of flags which denote program-wide configuration
  * parameters.
 */
static void
list_all_battery_info(char *infostr,
                      struct config *config)
{
        const char *sys_fs_path = SYS_FS_BATTERY_BASE_PATH;
        DIR *basedir = opendir(sys_fs_path);
        if (basedir == NULL) {
                fprintf(stderr, "error: couldn't open directory \"%s\": %s\n", sys_fs_path, strerror(errno));
                exit(1);
        }

        struct dirent *dir;
        int battery = 0;
        char path[SYS_FS_BATTERY_BASE_PATH_LEN + 264];

        battery_info_output_init(config);

        while ((dir = readdir(basedir)) != NULL) {
                if (/*!(dir->d_type & DT_DIR || dir->d_type & DT_LNK) ||*/ dir->d_name[0] == '.') {
                        continue;
                }

                // is this a battery path?
                set_path_to_dir();
                strcat(path, "/type");

                if (!compare_file_contents((const char*) path, "Battery")) {
                        // found a battery. was a specific battery name provided?
                        if (config->configflags & CONFIG_FLAG_BY_NAME) {
                                // does this name match?
                                if (!strcmp((const char*) dir->d_name, (const char*) config->cmdopts.n)) {
                                        // yes
                                        set_path_to_dir();
                                        list_battery_info(0, path, infostr, config);
                                        break;
                                } else {
                                        continue; // no
                                }
                        }

                        set_path_to_dir();
                        list_battery_info(battery, path, infostr, config);

                        battery++;
                }
        }

        closedir(basedir);

        battery_info_output_deinit(config);
}

#undef set_path_from_dir

//------------------------------------------------------------------------------
// _,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,__,-*'^'*-,_
//------------------------------------------------------------------------------

/** Routine which outputs program usage information to stderr, and then exits
 * the program.
 * \param retcode The return code to exit the program with.
 */
static void
usage(int retcode)
{
        fwrite((void*) usage_str, 1, sizeof(usage_str) - 1, stderr);
        exit(retcode);
}

/** Routine which outputs a small piece of program usage information to stderr,
 * and then exits the program with the specified return code.
 * \param retcode The return code to exit the program with.
 */
static void
usage_short(int retcode)
{
        fwrite((void*) short_usage_str, 1, sizeof(short_usage_str) - 1, stderr);
        exit(retcode);
}

/** Routine which outputs the program version to stderr, then exits the program
 * with return code 0. */
static void
version()
{
        fwrite((void*) version_str, 1, sizeof(version_str) - 1, stderr);
        exit(EXIT_SUCCESS);
}

/** Routine which outputs the program's copyright and license information, then
 * exits the program with return code 0.
 *
 * This is built into the final built binary so that it (the binary) can be
 * freely distributed by itself and still comply with the license.
 */
static void
license()
{
        fwrite((void*) license_str, 1, sizeof(license_str) - 1, stderr);
        exit(EXIT_SUCCESS);
}

/** Program entry point.
 * \param argc The amount of command-line arguments.
 * \param argv An array of the command-line arguments.
 */
int
main(int argc,
     char **argv)
{
        opterr = 0;

        char *infostr = (char*) DEFAULT_OUTPUT_SEQUENCE;

        struct config config;
        config_init(&config);

        if (argc > 1) {
                // parse arguments
                int c;
                char *infoflagstr = NULL;
                while ((c = getopt_long(argc, argv, "-hvladjn:N", (const struct option*) long_command_line_opts, NULL)) != -1) {
                        switch (c) {
                                case 'h':
                                        usage(EXIT_SUCCESS);
                                case 'v':
                                        version();
                                case 'l':
                                        license();
                                case 'a': {
                                        config.configflags |= CONFIG_FLAG_OUTPUT_ALL;
                                        break;
                                }
                                case 'd': {
                                        config.configflags |= CONFIG_FLAG_DIGITS;
                                        break;
                                }
                                case 'j': {
                                        config.output_format = OUTPUT_FORMAT_JSON;
                                        break;
                                }
                                case 'n': {
                                        if (strlen((const char*) optarg) < 1) {
                                                fprintf(stderr, "error: battery name must be a non-empty string for argument `-n'.\n");
                                                exit(EXIT_FAILURE);
                                        }
                                        config.cmdopts.n = optarg;
                                        config.configflags |= CONFIG_FLAG_BY_NAME;
                                        break;
                                }
                                case 'N': {
                                        config.configflags |= CONFIG_FLAG_DISABLE_CHARGE_CAP;
                                        break;
                                }
                                case '?': {
                                        fprintf(stderr, "error: invalid option specified -- `%c'\n", (char) optopt);
                                        usage_short(EXIT_FAILURE);
                                }
                                case 1: {
                                        if (infoflagstr != NULL) {
                                                usage_short(EXIT_FAILURE);
                                        }
                                        infoflagstr = optarg;
                                        break;
                                }
                        }
                }

                if (infoflagstr != NULL && !(config.configflags & CONFIG_FLAG_OUTPUT_ALL)) {
                        // if CONFIG_FLAG_OUTPUT_ALL is set, it overwrites
                        // whatever the user specifies for the output sequence,
                        // so skip checking it if it was provided.
                        char *p = infoflagstr;
                        while (*p != '\0') {
                                switch ((int) *p) {
                                        case 'n':
                                        case 'c':
                                        case 't':
                                        case 'v':
                                        case 'C':
                                        case 'T':
                                        case 'D':
                                        case 'd':
                                        case 'm':
                                        case 'M':
                                        case 'e':
                                        case 's':
                                        case 'h':
                                        case 'S':
                                        case 'H':
                                        case 'r':
                                        case 'p':
                                        case 'o':
                                        case 'g':
                                                break;
                                        default:
                                                fprintf(stderr, "error: unrecognised character -- '%c'\n", *p);
                                                usage_short(EXIT_FAILURE);
                                }
                                p++;
                        }
                        infostr = infoflagstr;
                }
        }

        list_all_battery_info(infostr, &config);

        return 0;
}
