#pragma once
#include <cstdint>
#include <string_view>
enum class Job_Status: std::uint8_t{
    PENDING,
    RUNNING,
    QUEUED,
    SUCCESS,
    FAILED,
    SKIPPED,
};
std::string_view to_string(Job_Status);
bool can_transition(Job_Status from, Job_Status to) {
    switch (from) {
        case Job_Status::PENDING:
            return to == Job_Status::QUEUED || to == Job_Status::SKIPPED;

        case Job_Status::QUEUED:
            return to == Job_Status::RUNNING
                || to == Job_Status::FAILED
                || to == Job_Status::SKIPPED;

        case Job_Status::RUNNING:
            return to == Job_Status::SUCCESS || to == Job_Status::FAILED;

        default:
            // SUCCESS, FAILED, SKIPPED are terminal — nothing is legal from here
            return false;
    }
}