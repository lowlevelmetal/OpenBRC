#include "engine/Task.h"

#include <algorithm>

namespace brc::engine {

Task::Task(TaskList& list) : m_list(list) { m_list.add(this); }
Task::~Task() { m_list.remove(this); }

void TaskList::remove(Task* t) {
    for (auto& e : m_tasks)
        if (e == t)
            e = nullptr;
    if (m_depth == 0)
        std::erase(m_tasks, nullptr);
}

std::size_t TaskList::size() const {
    return static_cast<std::size_t>(std::ranges::count_if(m_tasks, [](const Task* t) { return t != nullptr; }));
}

void TaskList::runPass() {
    ++m_depth;
    for (std::size_t i = 0; i < m_tasks.size(); ++i) {
        Task* t = m_tasks[i];
        if (t && !t->suspended())
            t->spendTime();
    }
    if (--m_depth == 0)
        std::erase(m_tasks, nullptr);
}

} // namespace brc::engine
