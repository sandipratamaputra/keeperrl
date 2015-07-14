#ifndef _TASK_MAP_H
#define _TASK_MAP_H

#include "util.h"
#include "entity_set.h"
#include "cost_info.h"
#include "position_map.h"

class Task;
class Creature;

class TaskMap {
  public:
  TaskMap(const vector<Level*>&);
  Task* addTask(PTask, const Creature*);
  Task* addPriorityTask(PTask, const Creature*);
  Task* addTask(PTask, Position);
  Task* getTask(const Creature*);
  bool hasTask(const Creature*) const;
  const vector<Task*>& getTasks(Position) const;
  vector<const Task*> getAllTasks() const;
  const Creature* getOwner(const Task*) const;
  optional<Position> getPosition(Task*) const;
  void takeTask(const Creature*, Task*);
  void freeTask(Task*);
  void freeFromTask(Creature*);

  Task* addTaskCost(PTask, Position, CostInfo);
  void markSquare(Position, HighlightType, PTask);
  void unmarkSquare(Position);
  Task* getMarked(Position) const;
  HighlightType getHighlightType(Position) const;
  CostInfo removeTask(Task*);
  CostInfo removeTask(UniqueEntity<Task>::Id);
  bool isPriorityTask(const Task*) const;
  bool hasPriorityTasks(Position) const;
  void freeTaskDelay(Task*, double delayTime);
  void setPriorityTasks(Position);
  Task* getTaskForWorker(Creature* c);
  const map<Task*, CostInfo>& getCompletionCosts() const;

  SERIALIZATION_DECL(TaskMap);

  private:
  BiMap<const Creature*, Task*> SERIAL(creatureMap);
  unordered_map<Task*, Position> SERIAL(positionMap);
  PositionMap<vector<Task*>> SERIAL(reversePositions);
  vector<PTask> SERIAL(tasks);
  PositionMap<Task*> SERIAL(marked);
  PositionMap<HighlightType> SERIAL(highlight);
  map<Task*, CostInfo> SERIAL(completionCost);
  map<UniqueEntity<Creature>::Id, double> SERIAL(delayedTasks);
  EntitySet<Task> SERIAL(priorityTasks);
};

#endif
