#include <gtest/gtest.h>

#include <string>

#include "job_status.hpp"

TEST(JobStatus, StringValuesMatchEnum) {
    EXPECT_EQ(std::string(to_string(Job_Status::PENDING)), "PENDING");
    EXPECT_EQ(std::string(to_string(Job_Status::QUEUED)), "QUEUED");
    EXPECT_EQ(std::string(to_string(Job_Status::RUNNING)), "RUNNING");
    EXPECT_EQ(std::string(to_string(Job_Status::SUCCESS)), "SUCCESS");
    EXPECT_EQ(std::string(to_string(Job_Status::FAILED)), "FAILED");
    EXPECT_EQ(std::string(to_string(Job_Status::SKIPPED)), "SKIPPED");
}

TEST(JobStatus, LegalTransitionsAreAllowed) {
    EXPECT_TRUE(can_transition(Job_Status::PENDING, Job_Status::QUEUED));
    EXPECT_TRUE(can_transition(Job_Status::PENDING, Job_Status::SKIPPED));
    EXPECT_TRUE(can_transition(Job_Status::QUEUED, Job_Status::RUNNING));
    EXPECT_TRUE(can_transition(Job_Status::QUEUED, Job_Status::FAILED));
    EXPECT_TRUE(can_transition(Job_Status::QUEUED, Job_Status::SKIPPED));
    EXPECT_TRUE(can_transition(Job_Status::RUNNING, Job_Status::SUCCESS));
    EXPECT_TRUE(can_transition(Job_Status::RUNNING, Job_Status::FAILED));
}

TEST(JobStatus, IllegalTransitionsAreRejected) {
    EXPECT_FALSE(can_transition(Job_Status::SUCCESS, Job_Status::FAILED));
    EXPECT_FALSE(can_transition(Job_Status::FAILED, Job_Status::SUCCESS));
    EXPECT_FALSE(can_transition(Job_Status::PENDING, Job_Status::RUNNING));
    EXPECT_FALSE(can_transition(Job_Status::QUEUED, Job_Status::PENDING));
}
