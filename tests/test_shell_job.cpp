#include <gtest/gtest.h>

#include "shell_job.hpp"

TEST(ShellJob, ExitZeroSucceeds) {
    ShellJob job("shell-success", "exit 0", 1);

    const auto result = job.execute();

    EXPECT_EQ(result.status, Job_Status::SUCCESS);
    EXPECT_EQ(result.exit_code, 0);
}

TEST(ShellJob, NonzeroExitFailsWithRealExitCode) {
    ShellJob job("shell-failure", "exit 3", 1);

    const auto result = job.execute();

    EXPECT_EQ(result.status, Job_Status::FAILED);
    EXPECT_EQ(result.exit_code, 3);
}

TEST(ShellJob, SignalExitUsesShellConvention) {
    ShellJob job("shell-signal", "kill -9 $$", 1);

    const auto result = job.execute();

    EXPECT_EQ(result.status, Job_Status::FAILED);
    EXPECT_EQ(result.exit_code, 137);
}
