#pragma once

#include <cstddef>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class DAG {
public:
    void add_job(const std::string& id) {
        if (indegree_.try_emplace(id, 0).second) {
            registration_order_.push_back(id);
        }
    }

    void add_edge(const std::string& from, const std::string& to) {
        add_job(from);
        add_job(to);
        dependents_[from].push_back(to);
        indegree_[to]++;
    }

    std::vector<std::string> initial_ready() const {
        std::vector<std::string> ready;
        for (const auto& id : registration_order_) {
            if (indegree_.at(id) == 0) {
                ready.push_back(id);
            }
        }
        return ready;
    }

    std::vector<std::string> mark_finished(const std::string& id) {
        const auto it = dependents_.find(id);
        if (it == dependents_.end()) {
            return {};
        }

        std::vector<std::string> ready;
        for (const auto& dependent : it->second) {
            --indegree_[dependent];
            if (indegree_[dependent] == 0 &&
                skipped_.find(dependent) == skipped_.end()) {
                ready.push_back(dependent);
            }
        }

        return ready;
    }

    std::vector<std::string> transitive_dependents(const std::string& id) const {
        std::vector<std::string> result;
        std::queue<std::string> pending;
        std::unordered_set<std::string> visited;

        const auto it = dependents_.find(id);
        if (it == dependents_.end()) {
            return result;
        }
        visited.insert(id);
        for (const auto& dependent : it->second) {
            if (visited.insert(dependent).second) {
                pending.push(dependent);
                result.push_back(dependent);
            }
        }

        while (!pending.empty()) {
            const auto current = pending.front();
            pending.pop();
            const auto current_it = dependents_.find(current);
            if (current_it == dependents_.end()) {
                continue;
            }
            for (const auto& dependent : current_it->second) {
                if (visited.insert(dependent).second) {
                    pending.push(dependent);
                    result.push_back(dependent);
                }
            }
        }

        return result;
    }

    bool mark_skipped(const std::string& id) {
        return skipped_.insert(id).second;
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
    std::vector<std::string> registration_order_;
    std::unordered_set<std::string> skipped_;
};
