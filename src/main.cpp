#include <iostream>
#include "job_status.hpp"
#include "job_run.hpp"
#include "StubJob.cpp"
void check(Job_Status from, Job_Status to) {
    std::cout << to_string(from) << " -> " << to_string(to)
              << " : " << (can_transition(from, to) ? "legal" : "illegal")
              << '\n';
}

int main() {
    // a few that should be legal
    check(Job_Status::PENDING, Job_Status::QUEUED);
    check(Job_Status::QUEUED, Job_Status::RUNNING);
    check(Job_Status::RUNNING, Job_Status::SUCCESS);
    check(Job_Status::QUEUED, Job_Status::SKIPPED);

    // a few that should be illegal
    check(Job_Status::RUNNING, Job_Status::QUEUED);
    check(Job_Status::SUCCESS, Job_Status::RUNNING); // terminal state
    check(Job_Status::PENDING, Job_Status::RUNNING);  // must pass through QUEUED
    StubJob stub("job-1", 3);
JobRun run(stub.id());

run.transition_to(Job_Status::QUEUED);
run.transition_to(Job_Status::RUNNING);

Job_Status result = stub.execute();
run.transition_to(result);

std::cout << "Final status: " << to_string(run.status()) << '\n';
}