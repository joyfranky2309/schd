#pragma once

#include <string>
#include <unordered_map>
#include <vector>

class DAG {
public:
    void add_edge(const std::string& from, const std::string& to) {
        
        dependents_[from].push_back(to);
        
        indegree_[to]++;
        
        indegree_.try_emplace(from,0);
    }

private:
    std::unordered_map<std::string, std::vector<std::string>> dependents_;
    std::unordered_map<std::string, int> indegree_;
};
