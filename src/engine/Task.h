#pragma once

#include <cstdint>
#include <vector>

namespace brc::engine {

class TaskList;

// OMediaPeriodical: an idle task. Every pass of the main loop calls
// spendTime() of each task that is not suspended, in creation order.
class Task {
public:
    explicit Task(TaskList& list);
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    virtual ~Task();

    virtual void spendTime() = 0;

    bool suspended() const { return m_suspended; }
    void setSuspended(bool s) { m_suspended = s; }

private:
    TaskList& m_list;
    bool m_suspended = false;
};

class TaskList {
public:
    // Runs every task once (0x45c130). Tasks created during the pass after the
    // current position run in the same pass; tasks destroyed during the pass
    // are skipped.
    void runPass();
    std::size_t size() const;

private:
    friend class Task;
    void add(Task* t) { m_tasks.push_back(t); }
    void remove(Task* t);

    std::vector<Task*> m_tasks;
    int m_depth = 0;
};

} // namespace brc::engine
