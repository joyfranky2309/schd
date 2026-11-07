#pragma once

#include <cstddef>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

class DAG {
public:
    void add_job(const std::string& id) {
        indegree_.try_emplace(id, 0);
    }

    void add_edge(const std::string& from, const std::string& to) {
        
        dependents_[from].push_back(to);
        
        indegree_[to]++;
        
        indegree_.try_emplace(from,0);
    }

    std::vector<std::string> initial_ready() const {
        
        std::vector<std::string>ready;
        for(const auto&[id,degree]:indegree_){
            if(degree==0){
                ready.push_back(id);
            }
        }
        return ready;
    }

    std::vector<std::string> mark_finished(const std::string& id) {
        
        auto it = dependents_.find(id);
        if(it==dependents_.end())return {};
        
        std::vector<std::string>ready;
        for(const auto& dep:it->second){
            indegree_[dep]--;
            if(indegree_[dep]==0){
               ready.push_back(dep);
            }
        }
        
        return ready;
    }

    std::vector<std::string> dependents_of(const std::string& id) const {
        const auto it = dependents_.find(id);
        if (it == dependents_.end()) {
            return {};
        }
        return it->second;
    }

    bool has_cycle() const {
        auto temp = indegree_;
        std::queue<std::string> q;

        for (const auto& [id, degree] : temp) {
            if (degree == 0) {
                q.push(id);
            }
        }

        std::size_t processed = 0;
        while (!q.empty()) {
            const auto id = q.front();
            q.pop();
            ++processed;

            const auto it = dependents_.find(id);
            if (it == dependents_.end()) {
                continue;
            }

            for (const auto& dep : it->second) {
                if (--temp[dep] == 0) {
                    q.push(dep);
                }
            }
        }

        return processed < indegree_.size();
    }

private:
    std::unordered_map<std::string, std::vector<std::string>> dependents_;
    std::unordered_map<std::string, int> indegree_;
};
