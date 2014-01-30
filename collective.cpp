#include "stdafx.h"

#include "collective.h"
#include "level.h"
#include "task.h"
#include "player.h"
#include "message_buffer.h"
#include "model.h"

enum Warning { NO_MANA, NO_CHESTS, MORE_CHESTS, NO_BEDS, MORE_BEDS, NO_TRAINING };
static const int numWarnings = 7;
static bool warning[numWarnings] = {0};
static string warningText[] {
  "You need to kill some innocent beings for more mana.",
  "You need to build a treasure room.",
  "You need a larger treasure room.",
  "You need a lair for your minions.",
  "You need a larger lair for your minions.",
  "You need training posts for your minions"};


vector<Collective::BuildInfo> Collective::initialBuildInfo {
    BuildInfo({SquareType::FLOOR, ResourceId::GOLD, 0, "Dig"}),
    BuildInfo({SquareType::KEEPER_THRONE, ResourceId::GOLD, 0, "Throne"}),
}; 

vector<Collective::BuildInfo> Collective::normalBuildInfo {
    BuildInfo({SquareType::FLOOR, ResourceId::GOLD, 0, "Dig"}),
    BuildInfo(BuildInfo::CUT_TREE),
    BuildInfo({SquareType::STOCKPILE, ResourceId::GOLD, 0, "Storage"}),
    BuildInfo({SquareType::TREASURE_CHEST, ResourceId::WOOD, 4, "Treasure room"}),
    BuildInfo({SquareType::TRIBE_DOOR, ResourceId::WOOD, 4, "Door"}),
    BuildInfo({SquareType::BED, ResourceId::WOOD, 8, "Lair"}),
    BuildInfo({SquareType::TRAINING_DUMMY, ResourceId::WOOD, 18, "Training room"}),
    BuildInfo({SquareType::LIBRARY, ResourceId::WOOD, 18, "Library"}),
    BuildInfo({SquareType::LABORATORY, ResourceId::WOOD, 18, "Laboratory"}),
    BuildInfo({SquareType::WORKSHOP, ResourceId::WOOD, 12, "Workshop"}),
    BuildInfo({SquareType::ANIMAL_TRAP, ResourceId::WOOD, 12, "Beast trap"}),
    BuildInfo({SquareType::GRAVE, ResourceId::WOOD, 18, "Graveyard"}),
    BuildInfo({TrapType::BOULDER, "Boulder trap", ViewId::BOULDER}),
    BuildInfo({TrapType::POISON_GAS, "Gas trap", ViewId::GAS_TRAP}),
    BuildInfo(BuildInfo::DESTROY),
    BuildInfo(BuildInfo::IMP),
    BuildInfo(BuildInfo::GUARD_POST),
};

vector<MinionType> minionTypes {
  MinionType::IMP,
  MinionType::NORMAL,
  MinionType::UNDEAD,
  MinionType::GOLEM,
  MinionType::BEAST,
};

vector<Collective::BuildInfo>& Collective::getBuildInfo() const {
 /* if (!isThroneBuilt())
    return initialBuildInfo;
  else*/
    return normalBuildInfo;
};
Collective::ResourceInfo info;

const map<Collective::ResourceId, Collective::ResourceInfo> Collective::resourceInfo {
  {ResourceId::GOLD, { SquareType::TREASURE_CHEST, Item::typePredicate(ItemType::GOLD), ItemId::GOLD_PIECE}},
  {ResourceId::WOOD, { SquareType::STOCKPILE, Item::namePredicate("wood plank"), ItemId::WOOD_PLANK}}
};

vector<TechId> techIds {
  TechId::NECROMANCY, TechId::BEAST_TAMING, TechId::MATTER_ANIMATION, TechId::SPELLCASTING};

vector<Collective::ItemFetchInfo> Collective::getFetchInfo() const {
  return {
    {unMarkedItems(ItemType::CORPSE), SquareType::GRAVE, true, {}},
    {[this](const Item* it) {
        return minionEquipment.isItemUseful(it) && !markedItems.count(it);
      }, SquareType::STOCKPILE, false, {}},
    {[this](const Item* it) {
        return it->getName() == "wood plank" && !markedItems.count(it); },
      SquareType::STOCKPILE, false, {SquareType::TREE_TRUNK}},
  };
}

Collective::Collective(Model* m, CreatureFactory factory) 
    : minionFactory(factory), mana(100), model(m) {
  EventListener::addListener(this);
  // init the map so the values can be safely read with .at()
  mySquares[SquareType::TREE_TRUNK].clear();
  for (BuildInfo info : concat(initialBuildInfo, normalBuildInfo))
    if (info.buildType == BuildInfo::SQUARE)
      mySquares[info.squareInfo.type].clear();
    else if (info.buildType == BuildInfo::TRAP)
      trapMap[info.trapInfo.type].clear();
  credit = {
    {ResourceId::GOLD, 100},
    {ResourceId::WOOD, 40},
 //   {ResourceId::IRON, 0},
  };
  for (TechId id: techIds)
    techLevels[id] = 0;
  for (MinionType t : minionTypes)
    minionByType[t].clear();
}


const int basicImpCost = 20;
int startImpNum = -1;
const int minionLimit = 20;


void Collective::render(View* view) {
  if (possessed && (!possessed->isPlayer() || possessed->isDead())) {
    if (contains(team, possessed))
      removeElement(team, possessed);
    if ((possessed->isDead() || possessed->isSleeping()) && !team.empty()) {
      possess(team.front(), view);
    } else {
      view->setTimeMilli(possessed->getTime() * 300);
      view->clearMessages();
      ViewObject::setHallu(false);
      possessed = nullptr;
      team.clear();
      gatheringTeam = false;
      teamLevelChanges.clear();
 //     view->resetCenter();
    }
  }
  if (!possessed) {
    view->refreshView(this);
  } else
    view->stopClock();
  if (showWelcomeMsg) {
    view->refreshView(this);
    showWelcomeMsg = false;
    view->presentText("", "So it begins.\n \n<insert a story about an evil warlock who has been banished and will now take revenge on everyone>\n \nWalk around and find a suitable spot to dig into the mountain. Then press 'u' to leave the Keeper body, and instruct your imps to dig, cut trees and mine gold. Build the needed rooms to attract and train your minions, and when the time comes go out to war (or defend yourself).");
  }
}

bool Collective::isTurnBased() {
  return possessed != nullptr && possessed->isPlayer();
}

vector<pair<Item*, Vec2>> Collective::getTrapItems(TrapType type, set<Vec2> squares) const {
  vector<pair<Item*, Vec2>> ret;
  if (squares.empty())
    squares = mySquares.at(SquareType::WORKSHOP);
  for (Vec2 pos : squares) {
    vector<Item*> v = level->getSquare(pos)->getItems([type, this](Item* it) {
        return it->getTrapType() == type && !markedItems.count(it); });
    for (Item* it : v)
      ret.emplace_back(it, pos);
  }
  return ret;
}

ViewObject Collective::getResourceViewObject(ResourceId id) const {
  return ItemFactory::fromId(resourceInfo.at(id).itemId)->getViewObject();
}

static string getTechName(TechId id) {
  switch (id) {
    case TechId::BEAST_TAMING: return "beast taming";
    case TechId::MATTER_ANIMATION: return "golem animation";
    case TechId::NECROMANCY: return "necromancy";
    case TechId::SPELLCASTING: return "personal spells";
  }
  Debug(FATAL) << "pwofk";
  return "";
}

static ViewObject getTechViewObject(TechId id) {
  switch (id) {
    case TechId::BEAST_TAMING: return ViewObject(ViewId::BEAR, ViewLayer::CREATURE, "");
    case TechId::MATTER_ANIMATION: return ViewObject(ViewId::IRON_GOLEM, ViewLayer::CREATURE, "");
    case TechId::NECROMANCY: return ViewObject(ViewId::VAMPIRE_LORD, ViewLayer::CREATURE, "");
    case TechId::SPELLCASTING: return ViewObject(ViewId::SCROLL, ViewLayer::CREATURE, "");
  }
  Debug(FATAL) << "pwofk";
  return ViewObject();
}

static vector<ItemId> marketItems {
  ItemId::HEALING_POTION,
  ItemId::SLEEP_POTION,
  ItemId::BLINDNESS_POTION,
  ItemId::INVISIBLE_POTION,
  ItemId::POISON_POTION,
  ItemId::SLOW_POTION,
  ItemId::SPEED_POTION,
  ItemId::WARNING_AMULET,
  ItemId::HEALING_AMULET,
  ItemId::DEFENSE_AMULET,
  ItemId::FRIENDLY_ANIMALS_AMULET,
};

void Collective::handleMarket(View* view, int prevItem) {
  if (mySquares[SquareType::STOCKPILE].empty()) {
    view->presentText("Information", "You need a storage room to use the market.");
    return;
  }
  vector<View::ListElem> options;
  vector<PItem> items;
  for (ItemId id : marketItems) {
    PItem item = ItemFactory::fromId(id);
    Optional<View::ElemMod> mod;
    if (item->getPrice() > numGold(ResourceId::GOLD))
      mod = View::INACTIVE;
    options.push_back(View::ListElem(item->getName() + "    $" + convertToString(item->getPrice()), mod));
    if (!mod)
      items.push_back(std::move(item));
  }
  auto index = view->chooseFromList("Buy items", options, prevItem);
  if (!index)
    return;
  Vec2 dest = chooseRandom(mySquares[SquareType::STOCKPILE]);
  takeGold({ResourceId::GOLD, items[*index]->getPrice()});
  level->getSquare(dest)->dropItem(std::move(items[*index]));
  view->refreshView(this);
  handleMarket(view, *index);
}

static string getTechLevelName(int level) {
  CHECK(level >= 0 && level < 4);
  return vector<string>({"basic", "advanced", "expert", "master"})[level];
}

struct SpellLearningInfo {
  SpellId id;
  int techLevel;
};

vector<SpellLearningInfo> spellLearning {
    { SpellId::HEALING, 0 },
    { SpellId::SUMMON_INSECTS, 0},
    { SpellId::DECEPTION, 1},
    { SpellId::SPEED_SELF, 1},
    { SpellId::STR_BONUS, 1},
    { SpellId::DEX_BONUS, 2},
    { SpellId::FIRE_SPHERE_PET, 2},
    { SpellId::TELEPORT, 2},
    { SpellId::INVISIBILITY, 3},
    { SpellId::WORD_OF_POWER, 3},
};


void Collective::handlePersonalSpells(View* view) {
  vector<View::ListElem> options {
      View::ListElem("The Keeper can learn personal spells for use in combat and other situations. ", View::TITLE),
      View::ListElem("You can cast them with 's' when you are in control of the Keeper.", View::TITLE)};
  vector<SpellId> knownSpells;
  for (SpellInfo spell : heart->getSpells())
    knownSpells.push_back(spell.id);
  for (auto elem : spellLearning) {
    SpellInfo spell = Creature::getSpell(elem.id);
    Optional<View::ElemMod> mod;
    if (!contains(knownSpells, spell.id))
      mod = View::INACTIVE;
    options.push_back(View::ListElem(spell.name + "  level: " + getTechLevelName(elem.techLevel), mod));
  }
  view->presentList("Personal spells", options);
}

vector<Collective::SpawnInfo> raisingInfo {
  {CreatureId::SKELETON, 30, 0},
  {CreatureId::ZOMBIE, 50, 0},
  {CreatureId::MUMMY, 50, 1},
  {CreatureId::VAMPIRE, 50, 2},
  {CreatureId::VAMPIRE_LORD, 100, 3},
};

void Collective::handleNecromancy(View* view, int prevItem, bool firstTime) {
  int techLevel = techLevels[TechId::NECROMANCY];
  set<Vec2> graves = mySquares.at(SquareType::GRAVE);
  if (minions.size() >= minionLimit) {
    if (firstTime)
      view->presentText("Information", "You have reached the limit of the number of minions.");
    return;
  }
  if (graves.empty()) {
    if (firstTime)
      view->presentText("Information", "You need to build a graveyard and collect corpses to raise undead.");
    return;
  }
  if (graves.size() <= minionByType.at(MinionType::UNDEAD).size()) {
    if (firstTime)
      view->presentText("Information", "You need to build more graves first for your undead to sleep in.");
    return;
  }
  vector<pair<Vec2, Item*>> corpses;
  for (Vec2 pos : graves) {
    for (Item* it : level->getSquare(pos)->getItems([](const Item* it) {
        return it->getType() == ItemType::CORPSE && it->getCorpseInfo()->canBeRevived; }))
      corpses.push_back({pos, it});
  }
  if (corpses.empty()) {
    if (firstTime)
      view->presentText("Information", "You need to collect some corpses to raise undead.");
    return;
  }
  vector<View::ListElem> options;
  vector<pair<PCreature, int>> creatures;
  for (SpawnInfo info : raisingInfo) {
    Optional<View::ElemMod> mod;
    if (info.minLevel > techLevel || info.manaCost > mana)
      mod = View::INACTIVE;
    PCreature c = CreatureFactory::fromId(info.id, Tribe::player, MonsterAIFactory::collective(this));
    options.push_back(View::ListElem(c->getName() + "  mana: " + convertToString(info.manaCost) +
          "  level: " + getTechLevelName(info.minLevel), mod));
    if (!mod)
      creatures.push_back({std::move(c), info.manaCost});
  }
  auto index = view->chooseFromList("Necromancy level: " + getTechLevelName(techLevel) + ", " +
      convertToString(corpses.size()) + " bodies available", options, prevItem);
  if (!index)
    return;
  // TODO: try many corpses before failing
  auto elem = chooseRandom(corpses);
  PCreature& creature = creatures[*index].first;
  mana -= creatures[*index].second;
  for (Vec2 v : elem.first.neighbors8(true))
    if (level->getSquare(v)->canEnter(creature.get())) {
      level->getSquare(elem.first)->removeItems({elem.second});
      addCreature(creature.get(), MinionType::UNDEAD);
      level->addCreature(v, std::move(creature));
      break;
    }
  if (creature)
    messageBuffer.addMessage(MessageBuffer::important("You have failed to reanimate the corpse."));
  view->refreshView(this);
  handleNecromancy(view, *index, false);
}

vector<Collective::SpawnInfo> animationInfo {
  {CreatureId::CLAY_GOLEM, 30, 0},
  {CreatureId::STONE_GOLEM, 50, 1},
  {CreatureId::IRON_GOLEM, 50, 2},
  {CreatureId::LAVA_GOLEM, 100, 3},
};


void Collective::handleMatterAnimation(View* view) {
  handleSpawning(view, TechId::MATTER_ANIMATION, SquareType::LABORATORY,
      "You need to build a laboratory to animate golems.", "You need a larger laboratory.", "Golem animation",
      MinionType::GOLEM, animationInfo);
}

vector<Collective::SpawnInfo> tamingInfo {
  {CreatureId::RAVEN, 5, 0},
  {CreatureId::SPIDER, 20, 1},
  {CreatureId::WOLF, 30, 2},
  {CreatureId::CAVE_BEAR, 50, 3},
};

void Collective::handleBeastTaming(View* view) {
  handleSpawning(view, TechId::BEAST_TAMING, SquareType::ANIMAL_TRAP,
      "You need to build cages to trap beasts.", "You need more cages.", "Beast taming",
      MinionType::BEAST, tamingInfo);
}

void Collective::handleSpawning(View* view, TechId techId, SquareType spawnSquare,
    const string& info1, const string& info2, const string& title, MinionType minionType,
    vector<SpawnInfo> spawnInfo) {
  int techLevel = techLevels[techId];
  set<Vec2> cages = mySquares.at(spawnSquare);
  int prevItem = false;
  bool firstTime = true;
  while (1) {
    if (minions.size() >= minionLimit) {
      if (firstTime)
        view->presentText("Information", "You have reached the limit of the number of minions.");
      return;
    }
    if (cages.empty()) {
      if (firstTime)
        view->presentText("Information", info1);
      return;
    }
    if (cages.size() <= minionByType.at(minionType).size()) {
      if (firstTime)
        view->presentText("Information", info2);
      return;
    }

    vector<View::ListElem> options;
    vector<pair<PCreature, int>> creatures;
    for (SpawnInfo info : spawnInfo) {
      Optional<View::ElemMod> mod;
      if (info.minLevel > techLevel || info.manaCost > mana)
        mod = View::INACTIVE;
      PCreature c = CreatureFactory::fromId(info.id, Tribe::player, MonsterAIFactory::collective(this));
      options.push_back(View::ListElem(c->getName() + "  mana: " + convertToString(info.manaCost) + 
            "   level: " + getTechLevelName(info.minLevel), mod));
      if (!mod)
        creatures.push_back({std::move(c), info.manaCost});
    }
    auto index = view->chooseFromList(title + " level: " + getTechLevelName(techLevel), options, prevItem);
    if (!index)
      return;
    Vec2 pos = chooseRandom(cages);
    PCreature& creature = creatures[*index].first;
    mana -= creatures[*index].second;
    for (Vec2 v : pos.neighbors8(true))
      if (level->getSquare(v)->canEnter(creature.get())) {
        addCreature(creature.get(), minionType);
        level->addCreature(v, std::move(creature));
        break;
      }
    if (creature)
      messageBuffer.addMessage(MessageBuffer::important("The spell failed."));
    view->refreshView(this);
    prevItem = *index;
    firstTime = false;
  }
}

vector<int> techAdvancePoints { 1, 2, 3, 1000};

void Collective::handleLibrary(View* view) {
  if (mySquares.at(SquareType::LIBRARY).empty()) {
    view->presentText("", "You need to build a library to start research.");
    return;
  }
  vector<View::ListElem> options;
  int points = int(techCounter);
  options.push_back(View::ListElem("You have " + convertToString(techCounter) 
        + " knowledge points available.", View::TITLE));
  vector<TechId> availableTechs;
  for (TechId id : techIds) {
    Optional<View::ElemMod> mod;
    string text = getTechName(id) + ": " + getTechLevelName(techLevels.at(id));
    int neededPoints = techAdvancePoints[techLevels.at(id)];
    if (neededPoints < 1000)
      text += "  (" + convertToString(neededPoints) + " points to advance)";
    if (neededPoints <= points) {
      availableTechs.push_back(id);
    } else
      mod = View::INACTIVE;
    options.push_back(View::ListElem(text, mod));
  }
  auto index = view->chooseFromList("Library", options);
  if (!index)
    return;
  TechId id = availableTechs[*index];
  techCounter -= techAdvancePoints[techLevels.at(id)];
  ++techLevels[id];
  if (id == TechId::SPELLCASTING)
    for (auto elem : spellLearning)
      if (elem.techLevel == techLevels[id])
        heart->addSpell(elem.id);
  handleLibrary(view);
}

void Collective::refreshGameInfo(View::GameInfo& gameInfo) const {
  gameInfo.infoType = View::GameInfo::InfoType::BAND;
  View::GameInfo::BandInfo& info = gameInfo.bandInfo;
  info.number = creatures.size();
  info.name = "KeeperRL";
  info.buttons.clear();
  for (BuildInfo button : getBuildInfo())
    switch (button.buildType) {
      case BuildInfo::SQUARE: {
            BuildInfo::SquareInfo& elem = button.squareInfo;
            Optional<pair<ViewObject, int>> cost;
            if (elem.cost > 0)
              cost = {getResourceViewObject(elem.resourceId), elem.cost};
            info.buttons.push_back({
                SquareFactory::get(elem.type)->getViewObject(),
                elem.name,
                cost,
                (elem.cost > 0 ? "[" + convertToString(mySquares.at(elem.type).size()) + "]" : ""),
                elem.cost <= numGold(elem.resourceId) });
           }
           break;
      case BuildInfo::TRAP: {
             BuildInfo::TrapInfo& elem = button.trapInfo;
             int numTraps = getTrapItems(elem.type).size();
             info.buttons.push_back({
                 ViewObject(elem.viewId, ViewLayer::LARGE_ITEM, ""),
                 elem.name,
                 Nothing(),
                 "(" + convertToString(numTraps) + " ready)",
                 numTraps > 0});
           }
           break;
      case BuildInfo::CUT_TREE:
           info.buttons.push_back({
               ViewObject(ViewId::WOOD_PLANK, ViewLayer::CREATURE, ""), "Cut tree", Nothing(), "", true});
           break;
      case BuildInfo::IMP: {
           pair<ViewObject, int> cost = {ViewObject::mana(), getImpCost()};
           info.buttons.push_back({
               ViewObject(ViewId::IMP, ViewLayer::CREATURE, ""),
               "Imp",
               cost,
               "[" + convertToString(imps.size()) + "]",
               getImpCost() <= mana});
           break; }
      case BuildInfo::DESTROY:
           info.buttons.push_back({
               ViewObject(ViewId::DESTROY_BUTTON, ViewLayer::CREATURE, ""), "Remove construction", Nothing(), "",
                   true});
           break;
      case BuildInfo::GUARD_POST:
           info.buttons.push_back({
               ViewObject(ViewId::GUARD_POST, ViewLayer::CREATURE, ""), "Guard post", Nothing(), "", true});
           break;
    }
  info.activeButton = currentButton;
  info.tasks = minionTaskStrings;
  info.monsterHeader = "Monsters: " + convertToString(minions.size()) + " / " + convertToString(minionLimit);
  info.creatures.clear();
  for (Creature* c : minions)
    info.creatures.push_back(c);
  info.enemies.clear();
  for (Vec2 v : myTiles)
    if (Creature* c = level->getSquare(v)->getCreature())
      if (c->getTribe() != Tribe::player)
        info.enemies.push_back(c);
  info.numGold.clear();
  for (auto elem : resourceInfo)
    info.numGold.push_back({getResourceViewObject(elem.first), numGold(elem.first)});
  info.numGold.push_back({ViewObject::mana(), mana});
  info.warning = "";
  for (int i : Range(numWarnings))
    if (warning[i]) {
      info.warning = warningText[i];
      break;
    }
  info.time = heart->getTime();
  info.gatheringTeam = gatheringTeam;
  info.team.clear();
  for (Creature* c : team)
    info.team.push_back(c);
  info.techButtons.clear();
  for (TechId id : techIds) {
    info.techButtons.push_back({getTechViewObject(id), getTechName(id)});
  }
  info.techButtons.push_back({Nothing(), ""});
  info.techButtons.push_back({ViewObject(ViewId::LIBRARY, ViewLayer::CREATURE, ""), "library"});
}

const MapMemory& Collective::getMemory(const Level* l) const {
  return memory[l];
}

static ViewObject getTrapObject(TrapType type) {
  switch (type) {
    case TrapType::BOULDER: return ViewObject(ViewId::UNARMED_BOULDER_TRAP, ViewLayer::LARGE_ITEM, "Unarmed trap");
    case TrapType::POISON_GAS: return ViewObject(ViewId::UNARMED_GAS_TRAP, ViewLayer::LARGE_ITEM, "Unarmed trap");
  }
  return ViewObject(ViewId::UNARMED_GAS_TRAP, ViewLayer::LARGE_ITEM, "Unarmed trap");
}

ViewIndex Collective::getViewIndex(Vec2 pos) const {
  ViewIndex index = level->getSquare(pos)->getViewIndex(this);
  if (marked.count(pos))
    index.setHighlight(HighlightType::BUILD);
  if (!index.hasObject(ViewLayer::LARGE_ITEM)) {
    if (traps.count(pos))
      index.insert(getTrapObject(traps.at(pos).type));
    if (guardPosts.count(pos))
      index.insert(ViewObject(ViewId::GUARD_POST, ViewLayer::LARGE_ITEM, "Guard post"));
  }
  if (const Location* loc = level->getLocation(pos)) {
    if (loc->isMarkedAsSurprise() && loc->getBounds().middle() == pos && !memory[level].hasViewIndex(pos))
      index.insert(ViewObject(ViewId::UNKNOWN_MONSTER, ViewLayer::CREATURE, "Surprise"));
  }
  return index;
}

bool Collective::staticPosition() const {
  return false;
}

Vec2 Collective::getPosition() const {
  return heart->getPosition();
}

enum Selection { SELECT, DESELECT, NONE } selection = NONE;



void Collective::addTask(PTask task, Creature* c) {
  taken[task.get()] = c;
  taskMap[c] = task.get();
  addTask(std::move(task));
}

void Collective::addTask(PTask task) {
  tasks.push_back(std::move(task));
}

void Collective::delayTask(Task* task, double time) {
  CHECK(task->canTransfer());
  delayed.insert({task, time});
  if (taken.count(task)) {
    taskMap.erase(taken.at(task));
    taken.erase(task);
  }
}

void Collective::removeTask(Task* task) {
  if (marked.count(task->getPosition()))
    marked.erase(task->getPosition());
  for (int i : All(tasks))
    if (tasks[i].get() == task) {
      removeIndex(tasks, i);
      break;
    }
  if (taken.count(task)) {
    taskMap.erase(taken.at(task));
    taken.erase(task);
  }
}

void Collective::markSquare(Vec2 pos, SquareType type, CostInfo cost) {
  tasks.push_back(Task::construction(this, pos, type));
  marked[pos] = tasks.back().get();
  if (cost.value)
    completionCost[tasks.back().get()] = cost;
}

void Collective::unmarkSquare(Vec2 pos) {
  Task* t = marked.at(pos);
  if (completionCost.count(t)) {
    returnGold(completionCost.at(t));
    completionCost.erase(t);
  }
  removeTask(t);
  marked.erase(pos);
}

int Collective::numGold(ResourceId id) const {
  int ret = credit.at(id);
  for (Vec2 pos : mySquares.at(resourceInfo.at(id).storageType))
    ret += level->getSquare(pos)->getItems(resourceInfo.at(id).predicate).size();
  return ret;
}

void Collective::takeGold(CostInfo cost) {
  int num = cost.value;
  if (num == 0)
    return;
  CHECK(num > 0);
  if (credit.at(cost.id)) {
    if (credit.at(cost.id) >= num) {
      credit[cost.id] -= num;
      return;
    } else {
      num -= credit.at(cost.id);
      credit[cost.id] = 0;
    }
  }
  for (Vec2 pos : randomPermutation(mySquares[resourceInfo.at(cost.id).storageType])) {
    vector<Item*> goldHere = level->getSquare(pos)->getItems(resourceInfo.at(cost.id).predicate);
    for (Item* it : goldHere) {
      level->getSquare(pos)->removeItem(it);
      if (--num == 0)
        return;
    }
  }
  Debug(FATAL) << "Didn't have enough gold";
}

void Collective::returnGold(CostInfo amount) {
  if (amount.value == 0)
    return;
  CHECK(amount.value > 0);
  if (mySquares[resourceInfo.at(amount.id).storageType].empty()) {
    credit[amount.id] += amount.value;
  } else
    level->getSquare(chooseRandom(mySquares[resourceInfo.at(amount.id).storageType]))->
        dropItems(ItemFactory::fromId(resourceInfo.at(amount.id).itemId, amount.value));
}

int Collective::getImpCost() const {
  if (imps.size() < startImpNum)
    return 0;
  return basicImpCost * pow(2, double(imps.size() - startImpNum) / 5);
}

void Collective::possess(const Creature* cr, View* view) {
  view->stopClock();
  CHECK(contains(creatures, cr));
  CHECK(!cr->isDead());
  Creature* c = const_cast<Creature*>(cr);
  if (c->isSleeping())
    c->wakeUp();
  freeFromGuardPost(c);
  c->pushController(new Player(c, view, model, false, &memory));
  possessed = c;
  c->getLevel()->setPlayer(c);
}

bool Collective::canBuildDoor(Vec2 pos) const {
  Rectangle innerRect = level->getBounds().minusMargin(1);
  auto wallFun = [=](Vec2 pos) {
      return level->getSquare(pos)->canConstruct(SquareType::FLOOR) ||
          !pos.inRectangle(innerRect); };
  return !traps.count(pos) && pos.inRectangle(innerRect) && 
      ((wallFun(pos - Vec2(0, 1)) && wallFun(pos - Vec2(0, -1))) ||
       (wallFun(pos - Vec2(1, 0)) && wallFun(pos - Vec2(-1, 0))));
}

bool Collective::canPlacePost(Vec2 pos) const {
  return !guardPosts.count(pos) && !traps.count(pos) &&
      level->getSquare(pos)->canEnterEmpty(Creature::getDefault()) && memory[level].hasViewIndex(pos);
}
  
void Collective::freeFromGuardPost(const Creature* c) {
  for (auto& elem : guardPosts)
    if (elem.second.attender == c)
      elem.second.attender = nullptr;
}

void Collective::processInput(View* view) {
  CollectiveAction action = view->getClick();
  switch (action.getType()) {
    case CollectiveAction::GATHER_TEAM:
        if (gatheringTeam && !team.empty()) {
          possess(team[0], view);
          gatheringTeam = false;
          for (Creature* c : team) {
            freeFromGuardPost(c);
            if (c->isSleeping())
              c->wakeUp();
          }
        } else
          gatheringTeam = true;
        break;
    case CollectiveAction::CANCEL_TEAM: gatheringTeam = false; team.clear(); break;
    case CollectiveAction::MARKET: handleMarket(view); break;
    case CollectiveAction::TECHNOLOGY:
        if (action.getNum() < techIds.size())
          switch (techIds[action.getNum()]) {
            case TechId::NECROMANCY: handleNecromancy(view); break;
            case TechId::SPELLCASTING: handlePersonalSpells(view); break;
            case TechId::MATTER_ANIMATION: handleMatterAnimation(view); break;
            case TechId::BEAST_TAMING: handleBeastTaming(view); break;
            default: break;
          };
        if (action.getNum() == techIds.size() + 1)
          handleLibrary(view);
        break;
    case CollectiveAction::ROOM_BUTTON: currentButton = action.getNum(); break;
    case CollectiveAction::CREATURE_BUTTON: 
        if (!gatheringTeam)
          possess(action.getCreature(), view);
        else {
          if (contains(team, action.getCreature()))
            removeElement(team, action.getCreature());
          else
            team.push_back(const_cast<Creature*>(action.getCreature()));
        }
        break;
    case CollectiveAction::CREATURE_DESCRIPTION: messageBuffer.addMessage(MessageBuffer::important(
                                                       action.getCreature()->getDescription())); break;
    case CollectiveAction::GO_TO: {
        Vec2 pos = action.getPosition();
        if (!pos.inRectangle(level->getBounds()))
          return;
        if (selection == NONE) {
          if (Creature* c = level->getSquare(pos)->getCreature())
            if (contains(minions, c)) {
              possess(c, view);
              break;
            }
        }
        switch (getBuildInfo()[currentButton].buildType) {
          case BuildInfo::IMP:
              if (mana >= getImpCost() && selection == NONE) {
                selection = SELECT;
                PCreature imp = CreatureFactory::fromId(CreatureId::IMP, Tribe::player,
                    MonsterAIFactory::collective(this));
                for (Vec2 v : pos.neighbors8(true))
                  if (v.inRectangle(level->getBounds()) && level->getSquare(v)->canEnter(imp.get()) 
                      && canSee(v)) {
                    mana -= getImpCost();
                    addCreature(imp.get(), MinionType::IMP);
                    level->addCreature(v, std::move(imp));
                    break;
                  }
              }
              break;
          case BuildInfo::TRAP: {
                TrapType trapType = getBuildInfo()[currentButton].trapInfo.type;
                if (getTrapItems(trapType).size() > 0 && canPlacePost(pos) && myTiles.count(pos)){
                  traps[pos] = {trapType, false, false};
                  trapMap[trapType].push_back(pos);
                  updateTraps();
                }
              }
              break;
          case BuildInfo::DESTROY:
              selection = SELECT;
              if (level->getSquare(pos)->canDestroy() && myTiles.count(pos))
                level->getSquare(pos)->destroy(10000);
              level->getSquare(pos)->removeTriggers();
              if (Creature* c = level->getSquare(pos)->getCreature())
                if (c->getName() == "boulder")
                  c->die(nullptr, false);
              if (traps.count(pos)) {
                removeElement(trapMap.at(traps.at(pos).type), pos);
                traps.erase(pos);
              }
              break;
          case BuildInfo::GUARD_POST:
              if (guardPosts.count(pos) && selection != SELECT) {
                guardPosts.erase(pos);
                selection = DESELECT;
              }
              else if (canPlacePost(pos) && guardPosts.size() < minions.size() && selection != DESELECT) {
                guardPosts[pos] = {nullptr};
                selection = SELECT;
              }
              break;
          case BuildInfo::CUT_TREE:
              if (marked.count(pos) && selection != SELECT) {
                unmarkSquare(pos);
                selection = DESELECT;
                if (throneMarked == pos)
                  throneMarked = Nothing();
              } else {
                if (!marked.count(pos) && selection != DESELECT &&
                    level->getSquare(pos)->canConstruct(SquareType::TREE_TRUNK)) {
                  markSquare(pos, SquareType::TREE_TRUNK, {ResourceId::GOLD, 0});
                  selection = SELECT;
                }
              }
              break;
          case BuildInfo::SQUARE:
              if (marked.count(pos) && selection != SELECT) {
                unmarkSquare(pos);
                selection = DESELECT;
                if (throneMarked == pos)
                  throneMarked = Nothing();
              } else {
                BuildInfo::SquareInfo info = getBuildInfo()[currentButton].squareInfo;
                bool diggingSquare = !memory[level].hasViewIndex(pos) ||
                  (level->getSquare(pos)->canConstruct(info.type));
                if (!marked.count(pos) && selection != DESELECT && diggingSquare && 
                    numGold(info.resourceId) >= info.cost && 
                    (info.type != SquareType::KEEPER_THRONE || !throneMarked) &&
                    (info.type != SquareType::TRIBE_DOOR || canBuildDoor(pos)) &&
                    (info.type == SquareType::FLOOR || canSee(pos))) {
                  markSquare(pos, info.type, {info.resourceId, info.cost});
                  selection = SELECT;
                  takeGold({info.resourceId, info.cost});
                  if (info.type == SquareType::KEEPER_THRONE)
                    throneMarked = pos;
                }
              }
              break;
        }
      }
      break;
    case CollectiveAction::BUTTON_RELEASE: selection = NONE; break;

    default: break;
  }
}

void Collective::onConstructed(Vec2 pos, SquareType type) {
  if (!contains({SquareType::ANIMAL_TRAP, SquareType::TREE_TRUNK}, type))
    myTiles.insert(pos);
  CHECK(!mySquares[type].count(pos));
  mySquares[type].insert(pos);
  if (contains({SquareType::FLOOR, SquareType::BRIDGE}, type))
    locked.clear();
  if (marked.count(pos))
    marked.erase(pos);
  if (contains({SquareType::GRASS, SquareType::HILL}, type) && !mySquares.at(SquareType::STOCKPILE).empty()) {
    addTask(Task::bringItem(this, pos, level->getSquare(pos)->getItems(Item::namePredicate("wood plank")), 
        chooseRandom(mySquares.at(SquareType::STOCKPILE))));
  }
}

void Collective::onPickedUp(Vec2 pos, vector<Item*> items) {
  CHECK(!items.empty());
  for (Item* it : items)
    markedItems.erase(it);
}
  
void Collective::onCantPickItem(vector<Item*> items) {
  for (Item* it : items)
    markedItems.erase(it);
}

void Collective::onBrought(Vec2 pos, vector<Item*> items) {
}

void Collective::onAppliedItem(Vec2 pos, Item* item) {
  CHECK(item->getTrapType());
  if (traps.count(pos)) {
    traps[pos].marked = false;
    traps[pos].armed = true;
  }
}

void Collective::onAppliedSquare(Vec2 pos) {
  if (mySquares.at(SquareType::LIBRARY).count(pos))
    techCounter += Random.getDouble(0.01, 0.02);
}

void Collective::onAppliedItemCancel(Vec2 pos) {
  traps.at(pos).marked = false;
}

Vec2 Collective::getHeartPos() const {
  return heart->getPosition();
}

int Collective::getNumPoints() const {
  return points;
}

ItemPredicate Collective::unMarkedItems(ItemType type) const {
  return [this, type](const Item* it) {
      return it->getType() == type && !markedItems.count(it); };
}

void Collective::update(Creature* c) {
  if (!contains(creatures, c) || c->getLevel() != level)
    return;
  for (Vec2 pos : level->getVisibleTiles(c)) {
    ViewIndex index = level->getSquare(pos)->getViewIndex(c);
    memory[level].clearSquare(pos);
    for (ViewLayer l : { ViewLayer::ITEM, ViewLayer::FLOOR_BACKGROUND, ViewLayer::FLOOR, ViewLayer::LARGE_ITEM})
      if (index.hasObject(l))
        memory[level].addObject(pos, index.getObject(l));
  }
}

bool Collective::isDownstairsVisible() const {
  vector<Vec2> v = level->getLandingSquares(StairDirection::DOWN, StairKey::DWARF);
  return v.size() == 1 && memory[level].hasViewIndex(v[0]);
}

bool Collective::isThroneBuilt() const {
  return !mySquares.at(SquareType::KEEPER_THRONE).empty();
}

void Collective::updateTraps() {
  map<TrapType, vector<pair<Item*, Vec2>>> trapItems {
    {TrapType::BOULDER, getTrapItems(TrapType::BOULDER, myTiles)},
    {TrapType::POISON_GAS, getTrapItems(TrapType::POISON_GAS, myTiles)}};
  for (auto elem : traps) {
    vector<pair<Item*, Vec2>>& items = trapItems.at(elem.second.type);
    if (!items.empty()) {
      if (!elem.second.armed && !elem.second.marked) {
        addTask(Task::applyItem(this, items.back().second, items.back().first, elem.first));
        markedItems.insert({items.back().first});
        items.pop_back();
        traps[elem.first].marked = true;
      }
    }
  }
}

void Collective::tick() {
  if ((minionByType.at(MinionType::NORMAL).size() < mySquares[SquareType::BED].size() || minions.empty())
      && Random.roll(40) && minions.size() < minionLimit) {
    PCreature c = minionFactory.random(MonsterAIFactory::collective(this));
    addCreature(c.get());
    level->landCreature(StairDirection::UP, StairKey::PLAYER_SPAWN, std::move(c));
  }
  warning[NO_MANA] = mana < 20;
  warning[NO_BEDS] = mySquares[SquareType::BED].size() == 0 && !minions.empty();
  warning[MORE_BEDS] = mySquares[SquareType::BED].size() < minionByType.at(MinionType::NORMAL).size();
  warning[NO_TRAINING] = mySquares[SquareType::TRAINING_DUMMY].empty() && !minions.empty();
  updateTraps();
  for (Vec2 pos : myTiles) {
    if (Creature* c = level->getSquare(pos)->getCreature()) {
      if (!contains(creatures, c) && c->getTribe() == Tribe::player
          && !contains({"boulder"}, c->getName()))
        // We just found a friendly creature (and not a boulder nor a chicken)
        addCreature(c);
      if (c->getTribe() != Tribe::player)
        for (PTask& task : tasks)
          if (task->getPosition() == pos && task->canTransfer())
            delayTask(task.get(), c->getTime() + 50);
    }
    vector<Item*> gold = level->getSquare(pos)->getItems(unMarkedItems(ItemType::GOLD));
    if (gold.size() > 0 && !mySquares[SquareType::TREASURE_CHEST].count(pos)) {
      if (!mySquares[SquareType::TREASURE_CHEST].empty()) {
        warning[NO_CHESTS] = false;
        Optional<Vec2> target;
        for (Vec2 chest : mySquares[SquareType::TREASURE_CHEST])
          if ((!target || (chest - pos).length8() < (*target - pos).length8()) && 
              level->getSquare(chest)->getItems(Item::typePredicate(ItemType::GOLD)).size() <= 30)
            target = chest;
        if (!target)
          warning[MORE_CHESTS] = true;
        else {
          warning[MORE_CHESTS] = false;
          addTask(Task::bringItem(this, pos, gold, *target));
          markedItems.insert(gold.begin(), gold.end());
        }
      } else {
        warning[NO_CHESTS] = true;
      }
    }
    if (marked.count(pos) && marked.at(pos)->isImpossible(level) && !taken.count(marked.at(pos)))
      removeTask(marked.at(pos));
  }
  for (ItemFetchInfo elem : getFetchInfo()) {
    for (Vec2 pos : myTiles)
      fetchItems(pos, elem);
    for (SquareType type : elem.additionalPos)
      for (Vec2 pos : mySquares.at(type))
        fetchItems(pos, elem);
  }
}

void Collective::fetchItems(Vec2 pos, ItemFetchInfo elem) {
  vector<Item*> equipment = level->getSquare(pos)->getItems(elem.predicate);
  if (!equipment.empty() && !mySquares[elem.destination].empty() &&
      !mySquares[elem.destination].count(pos)) {
    if (elem.oneAtATime)
      equipment = {equipment[0]};
    Vec2 target = chooseRandom(mySquares[elem.destination]);
    addTask(Task::bringItem(this, pos, equipment, target));
    markedItems.insert(equipment.begin(), equipment.end());
  }
}


bool Collective::canSee(const Creature* c) const {
  return canSee(c->getPosition());
}

bool Collective::canSee(Vec2 position) const {
  return memory[level].hasViewIndex(position)
      || contains(level->getLandingSquares(StairDirection::DOWN, StairKey::DWARF), position)
      || contains(level->getLandingSquares(StairDirection::UP, StairKey::DWARF), position);
  for (Creature* member : creatures)
    if (member->canSee(position))
      return true;
  return false;
}

void Collective::setLevel(Level* l) {
  for (Vec2 v : l->getBounds())
    if (/*contains({SquareApplyType::ASCEND, SquareApplyType::DESCEND},
            l->getSquare(v)->getApplyType(Creature::getDefault())) ||*/
        l->getSquare(v)->getName() == "gold ore")
      memory[l].addObject(v, l->getSquare(v)->getViewObject());
  level = l;
}

vector<const Creature*> Collective::getUnknownAttacker() const {
  return {};
}

void Collective::onChangeLevelEvent(const Creature* c, const Level* from, Vec2 pos, const Level* to, Vec2 toPos) {
  if (c == possessed) { 
    teamLevelChanges[from] = pos;
    if (!levelChangeHistory.count(to))
      levelChangeHistory[to] = toPos;
  }
}

MoveInfo Collective::getBeastMove(Creature* c) {
  if (!Random.roll(5))
    return NoMove;
  Vec2 radius(7, 7);
  for (Vec2 v : randomPermutation(Rectangle(c->getPosition() - radius, c->getPosition() + radius).getAllSquares()))
    if (!memory[level].hasViewIndex(v)) {
      if (auto move = c->getMoveTowards(v))
        return {1.0, [c, move]() { return c->move(*move); }};
      else
        return NoMove;
    }
  return NoMove;
}

MoveInfo Collective::getMinionMove(Creature* c) {
  if (possessed && contains(team, c)) {
    Optional<Vec2> v;
    if (possessed->getLevel() != c->getLevel()) {
      if (teamLevelChanges.count(c->getLevel())) {
        v = teamLevelChanges.at(c->getLevel());
        if (v == c->getPosition())
          return {1.0, [=] {
            c->applySquare();
          }};
      }
    } else 
      v = possessed->getPosition();
    if (v) {
      if (auto move = c->getMoveTowards(*v))
        return {1.0, [=] {
          c->move(*move);
        }};
      else
        return NoMove;
    }
  }
  if (c->getLevel() != level) {
    if (!levelChangeHistory.count(c->getLevel()))
      return NoMove;
    Vec2 target = levelChangeHistory.at(c->getLevel());
    if (c->getPosition() == target)
      return {1.0, [=] {
        c->applySquare();
      }};
    else if (auto move = c->getMoveTowards(target))
      return {1.0, [=] {
        c->move(*move);
      }};
    else
      return NoMove;
  }
  if (contains(minionByType.at(MinionType::BEAST), c))
    return getBeastMove(c);
  for (auto& elem : guardPosts) {
    bool isTraining = contains({MinionTask::TRAIN}, minionTasks.at(c).getState());
    if (elem.second.attender == c) {
      if (isTraining) {
        minionTasks.at(c).update();
        if (c->getPosition().dist8(elem.first) > 1) {
          if (auto move = c->getMoveTowards(elem.first))
            return {1.0, [=] {
              c->move(*move);
            }};
        } else
          return NoMove;
      } else
        elem.second.attender = nullptr;
    }
  }
  for (auto& elem : guardPosts) {
    bool isTraining = contains({MinionTask::TRAIN}, minionTasks.at(c).getState());
    if (elem.second.attender == nullptr && isTraining) {
      elem.second.attender = c;
      if (taskMap.count(c))
        removeTask(taskMap.at(c));
    }
  }
 
  if (taskMap.count(c)) {
    Task* task = taskMap.at(c);
    if (task->isDone()) {
      removeTask(task);
    } else
      return task->getMove(c);
  }
  for (Vec2 v : mySquares[SquareType::STOCKPILE])
    for (Item* it : level->getSquare(v)->getItems([this, c] (const Item* it) {
          return minionEquipment.needsItem(c, it); })) {
      if (c->canEquip(it)) {
        addTask(Task::equipItem(this, v, it), c);
      }
      else
        addTask(Task::pickItem(this, v, {it}), c);
      return taskMap.at(c)->getMove(c);
    }
  minionTasks.at(c).update();
  if (c->getHealth() < 1 && c->canSleep())
    minionTasks.at(c).setState(MinionTask::SLEEP);
  switch (minionTasks.at(c).getState()) {
    case MinionTask::SLEEP: {
        if (c == heart) {
          return {1.0, [c] {
            c->wait();
          }};
        }
        set<Vec2>& whatBeds = (contains(minionByType.at(MinionType::UNDEAD), c) ? 
            mySquares[SquareType::GRAVE] : mySquares[SquareType::BED]);
        if (whatBeds.empty())
          return NoMove;
        addTask(Task::applySquare(this, whatBeds), c);
        minionTaskStrings[c] = "sleeping";
        break; }
    case MinionTask::TRAIN:
        if (mySquares[SquareType::TRAINING_DUMMY].empty()) {
          minionTasks.at(c).update();
          return NoMove;
        }
        addTask(Task::applySquare(this, mySquares[SquareType::TRAINING_DUMMY]), c);
        minionTaskStrings[c] = "training";
        break;
    case MinionTask::WORKSHOP:
        if (mySquares[SquareType::WORKSHOP].empty()) {
          minionTasks.at(c).setState(MinionTask::SLEEP);
          return NoMove;
        }
        addTask(Task::applySquare(this, mySquares[SquareType::WORKSHOP]), c);
        minionTaskStrings[c] = "crafting";
        break;
    case MinionTask::STUDY:
        if (mySquares[SquareType::LIBRARY].empty()) {
          minionTasks.at(c).setState(MinionTask::SLEEP);
          return NoMove;
        }
        addTask(Task::applySquare(this, mySquares[SquareType::LIBRARY]), c);
        minionTaskStrings[c] = "researching";
        break;
    case MinionTask::EAT:
        if (mySquares[SquareType::HATCHERY].empty())
          return NoMove;
        minionTaskStrings[c] = "eating";
        addTask(Task::eat(this, mySquares[SquareType::HATCHERY]), c);
        break;
  }
  return taskMap.at(c)->getMove(c);
}

MoveInfo Collective::getMove(Creature* c) {
  CHECK(contains(creatures, c));
  if (!contains(imps, c)) {
    CHECK(contains(minions, c));
    return getMinionMove(c);
  }
  if (c->getLevel() != level)
    return NoMove;
  if (startImpNum == -1)
    startImpNum = imps.size();
  if (taskMap.count(c)) {
    Task* task = taskMap.at(c);
    if (task->isDone()) {
      removeTask(task);
    } else
      return task->getMove(c);
  }
  Task* closest = nullptr;
  for (PTask& task : tasks) {
    if (delayed.count(task.get())) { 
      if (delayed.at(task.get()) > c->getTime())
        continue;
      else 
        delayed.erase(task.get());
    }
    double dist = (task->getPosition() - c->getPosition()).length8();
    if ((!taken.count(task.get()) || (task->canTransfer() 
                                && (task->getPosition() - taken.at(task.get())->getPosition()).length8() > dist))
           && (!closest ||
           dist < (closest->getPosition() - c->getPosition()).length8()) && !locked.count(make_pair(c, task.get()))) {
      bool valid = task->getMove(c).isValid();
      if (valid)
        closest = task.get();
      else
        locked.insert(make_pair(c, task.get()));
    }
  }
  if (closest) {
    if (taken.count(closest)) {
      taskMap.erase(taken.at(closest));
      taken.erase(closest);
    }
    taskMap[c] = closest;
    taken[closest] = c;
    return closest->getMove(c);
  } else {
    if (!myTiles.count(c->getPosition()) && heart->getLevel() == c->getLevel()) {
      Vec2 heartPos = heart->getPosition();
      if (heartPos.dist8(c->getPosition()) < 3)
        return NoMove;
      if (auto move = c->getMoveTowards(heartPos))
        return {1.0, [=] {
          c->move(*move);
        }};
      else
        return NoMove;
    } else
      return NoMove;
  }
}

MarkovChain<MinionTask> Collective::getTasksForMinion(Creature* c) {
  MinionTask t1;
  if (c == heart)
    return MarkovChain<MinionTask>(MinionTask::SLEEP, {
      {MinionTask::SLEEP, {{ MinionTask::STUDY, 0.05}}},
      {MinionTask::STUDY, {{ MinionTask::SLEEP, 0.02}}}});
  if (contains(minionByType.at(MinionType::GOLEM), c))
    return MarkovChain<MinionTask>(MinionTask::TRAIN, {
      {MinionTask::TRAIN, {}}});

  if (c->getName() == "gnome") {
    t1 = MinionTask::WORKSHOP;
  } else {
    t1 = MinionTask::TRAIN;
  }
  return MarkovChain<MinionTask>(MinionTask::SLEEP, {
      {MinionTask::SLEEP, {{ MinionTask::EAT, 0.5}, { t1, 0.5}}},
      {MinionTask::EAT, {{ t1, 0.4}, { MinionTask::SLEEP, 0.2}}},
      {t1, {{ MinionTask::EAT, 0.005}, { MinionTask::SLEEP, 0.005}}}});
}

void Collective::addCreature(Creature* c, MinionType type) {
  if (heart == nullptr) {
    heart = c;
    for (auto elem : spellLearning)
      if (elem.techLevel == 0)
        heart->addSpell(elem.id);
  }
  creatures.push_back(c);
  if (type != MinionType::IMP) {
    minions.push_back(c);
    minionTasks.insert(make_pair(c, getTasksForMinion(c)));
    minionByType[type].push_back(c);
  } else {
    imps.push_back(c);
 //   c->addEnemyVision([this](const Creature* c) { return canSee(c); });
  }
}

void Collective::onSquareReplacedEvent(const Level* l, Vec2 pos) {
  if (l == level) {
    for (auto& elem : mySquares)
      if (elem.second.count(pos)) {
        elem.second.erase(pos);
      }
  }
}

void Collective::onTriggerEvent(const Level* l, Vec2 pos) {
  if (traps.count(pos) && l == level)
    traps.at(pos).armed = false;
}

void Collective::onConqueredLand(const string& name) {
  model->conquered(*heart->getFirstName() + " the Keeper", name, kills, points);
}

void Collective::onKillEvent(const Creature* victim, const Creature* killer) {
  if (victim == heart) {
    model->gameOver(heart, kills.size(), "innocent beings", points);
  }
  if (contains(creatures, victim)) {
    Creature* c = const_cast<Creature*>(victim);
    removeElement(creatures, c);
    for (auto& elem : guardPosts)
      if (elem.second.attender == c)
        elem.second.attender = nullptr;
    if (contains(team, c))
      removeElement(team, c);
    if (taskMap.count(c)) {
      if (!taskMap.at(c)->canTransfer()) {
        taskMap.at(c)->cancel();
        removeTask(taskMap.at(c));
      } else {
        taken.erase(taskMap.at(c));
        taskMap.erase(c);
      }
    }
    if (contains(imps, c))
      removeElement(imps, c);
    if (contains(minions, c))
      removeElement(minions, c);
    for (MinionType type : minionTypes)
      if (contains(minionByType.at(type), c))
        removeElement(minionByType.at(type), c);
  } else if (victim->getTribe() != Tribe::player) {
    double incMana = victim->getDifficultyPoints();
    mana += incMana;
    kills.push_back(victim);
    points += victim->getDifficultyPoints();
    Debug() << "Mana increase " << incMana << " from " << victim->getName();
    heart->increaseExpLevel(victim->getDifficultyPoints() / 300);
  }
}
  
const Level* Collective::getLevel() const {
  return level;
}
