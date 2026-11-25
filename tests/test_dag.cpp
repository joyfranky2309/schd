#include <gtest/gtest.h>
#include <vector>

#include "dag.hpp"

TEST(DAG, StandaloneJobsAreInitiallyReady) {
    DAG dag;
    dag.add_job("standalone-job");

    EXPECT_FALSE(dag.has_cycle());
    EXPECT_EQ(dag.initial_ready(), std::vector<std::string>{"standalone-job"});
}

TEST(DAG, InitiallyReadyJobsFollowRegistrationOrder) {
    DAG dag;
    dag.add_job("first");
    dag.add_job("dependent");
    dag.add_job("second");
    dag.add_edge("first", "dependent");

    EXPECT_EQ(dag.initial_ready(), (std::vector<std::string>{"first", "second"}));
}

TEST(DAG, CyclicGraphIsRejectedBeforeScheduling) {
    DAG dag;
    dag.add_edge("job-A", "job-B");
    dag.add_edge("job-B", "job-A");

    EXPECT_TRUE(dag.has_cycle());
}

TEST(DAG, TransitiveDependentsAreReturnedOnce) {
    DAG dag;
    dag.add_edge("job-A", "job-C");
    dag.add_edge("job-B", "job-C");
    dag.add_edge("job-C", "job-D");

    EXPECT_EQ(dag.transitive_dependents("job-A"), (std::vector<std::string>{"job-C", "job-D"}));
}

TEST(DAG, FinishedJobsNeverReleaseSkippedDependents) {
    DAG dag;
    dag.add_edge("job-A", "job-C");
    dag.add_edge("job-B", "job-C");
    ASSERT_TRUE(dag.mark_skipped("job-C"));
    EXPECT_FALSE(dag.mark_skipped("job-C"));

    EXPECT_TRUE(dag.mark_finished("job-A").empty());
    EXPECT_TRUE(dag.mark_finished("job-B").empty());
}
