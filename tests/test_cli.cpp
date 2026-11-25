#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <system_error>
#include <tuple>
#include <unistd.h>
#include <utility>

namespace {
class TemporaryFile {
public:
    TemporaryFile(const std::string& suffix, const std::string& contents) {
        path_ = std::filesystem::temp_directory_path() /
                ("schd-cli-" + std::to_string(::getpid()) + "-" + suffix);
        std::ofstream output(path_);
        if (!output) {
            throw std::runtime_error("could not create temporary CLI file");
        }
        output << contents;
        if (!output) {
            throw std::runtime_error("could not write temporary CLI file");
        }
    }

    ~TemporaryFile() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

struct ProcessResult {
    int exit_code;
    std::string output;
};

ProcessResult run_cli(const std::filesystem::path& input_path,
                      const std::filesystem::path& output_path, bool verbose = false) {
    const pid_t child_pid = ::fork();
    if (child_pid == -1) {
        throw std::runtime_error("fork failed");
    }
    if (child_pid == 0) {
        const int output_fd = ::open(output_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        const int error_fd = ::open("/dev/null", O_WRONLY);
        if (output_fd == -1 || ::dup2(output_fd, STDOUT_FILENO) == -1 || error_fd == -1 ||
            ::dup2(error_fd, STDERR_FILENO) == -1) {
            ::_exit(126);
        }
        ::close(output_fd);
        ::close(error_fd);
        if (verbose) {
            ::execl(SCHD_CLI_PATH, SCHD_CLI_PATH, "run", input_path.c_str(), "--workers", "2",
                    "--verbose", static_cast<char*>(nullptr));
        } else {
            ::execl(SCHD_CLI_PATH, SCHD_CLI_PATH, "run", input_path.c_str(), "--workers", "2",
                    static_cast<char*>(nullptr));
        }
        ::_exit(127);
    }

    int status = 0;
    pid_t waited_pid = 0;
    do {
        waited_pid = ::waitpid(child_pid, &status, 0);
    } while (waited_pid == -1 && errno == EINTR);
    if (waited_pid == -1) {
        throw std::runtime_error("waitpid failed for CLI process");
    }

    std::ifstream output_file(output_path);
    std::ostringstream output;
    output << output_file.rdbuf();
    if (!WIFEXITED(status)) {
        throw std::runtime_error("CLI process did not exit normally");
    }
    return ProcessResult{WEXITSTATUS(status), output.str()};
}
} // namespace

TEST(Cli, RunReportsFailureAndSkippedDependentWithExitOne) {
    TemporaryFile input("input.json",
                        R"json({
            "workers": 1,
            "jobs": [
                {"id":"failed","type":"shell","command":"exit 3","category":"EOD"},
                {"id":"skipped","type":"shell","command":"exit 0","category":"EOD",
                 "depends_on":["failed"]},
                {"id":"independent","type":"shell","command":"exit 0","category":"ADHOC"}
            ]
        })json");
    TemporaryFile output("output.txt", "");

    const auto result = run_cli(input.path(), output.path());

    EXPECT_EQ(result.exit_code, 1);
    std::istringstream lines(result.output);
    std::string line;
    std::getline(lines, line);
    EXPECT_NE(line.find("ID"), std::string::npos);
    EXPECT_NE(line.find("STATUS"), std::string::npos);
    EXPECT_NE(line.find("ATTEMPTS"), std::string::npos);
    EXPECT_NE(line.find("DURATION_MS"), std::string::npos);
    std::getline(lines, line);

    std::map<std::string, std::tuple<std::string, int, int>> rows;
    while (std::getline(lines, line)) {
        std::istringstream row(line);
        std::string id;
        std::string status;
        int attempts = -1;
        int duration_ms = -1;
        if (row >> id >> status >> attempts >> duration_ms) {
            rows.emplace(id, std::make_tuple(status, attempts, duration_ms));
        }
    }
    ASSERT_EQ(rows.size(), 3U);
    EXPECT_EQ(std::get<0>(rows.at("failed")), "FAILED");
    EXPECT_EQ(std::get<1>(rows.at("failed")), 1);
    EXPECT_GE(std::get<2>(rows.at("failed")), 0);
    EXPECT_EQ(std::get<0>(rows.at("skipped")), "SKIPPED");
    EXPECT_EQ(std::get<1>(rows.at("skipped")), 0);
    EXPECT_GE(std::get<2>(rows.at("skipped")), 0);
    EXPECT_EQ(std::get<0>(rows.at("independent")), "SUCCESS");
    EXPECT_EQ(std::get<1>(rows.at("independent")), 1);
    EXPECT_GE(std::get<2>(rows.at("independent")), 0);
}

TEST(Cli, VerboseRunPrintsCpuPricingMessage) {
    TemporaryFile input("pricing-input.json",
                        R"json({
            "jobs": [{
                "id": "cpu-price",
                "type": "cpu_pricing",
                "category": "EOD",
                "params": {
                    "face_value": 100,
                    "coupon_rate": 0.05,
                    "maturity_years": 5,
                    "yield": 0.05
                }
            }]
        })json");
    TemporaryFile output("pricing-output.txt", "");

    const auto result = run_cli(input.path(), output.path(), true);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.output.find("cpu-price: price="), std::string::npos);
}

#ifdef SCHD_WITH_QUANTLIB
TEST(Cli, VerboseRunPrintsQuantLibBondMessage) {
    TemporaryFile input("quantlib-input.json",
                        R"json({
            "jobs": [{
                "id": "bond-price",
                "type": "quantlib_bond",
                "category": "EOD",
                "params": {
                    "face_value": 100,
                    "coupon_rate": 0.05,
                    "maturity_years": 5,
                    "yield": 0.05,
                    "settlement_date": "2024-01-02"
                }
            }]
        })json");
    TemporaryFile output("quantlib-output.txt", "");

    const auto result = run_cli(input.path(), output.path(), true);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.output.find("bond-price: npv="), std::string::npos);
    EXPECT_NE(result.output.find("clean_price="), std::string::npos);
}
#endif
