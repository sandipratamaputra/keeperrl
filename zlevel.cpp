#include "zlevel.h"
#include "content_factory.h"
#include "resource_counts.h"
#include "z_level_info.h"
#include "enemy_factory.h"
#include "collective_builder.h"
#include "level_maker.h"
#include "attack_trigger.h"
#include "tribe_alignment.h"
#include "external_enemies.h"

static EnemyInfo getEnemy(EnemyId id, ContentFactory* contentFactory) {
  auto enemy = EnemyFactory(Random, contentFactory->getCreatures().getNameGenerator(), contentFactory->enemies,
      contentFactory->buildingInfo, {}).get(id);
  enemy.settlement.collective = new CollectiveBuilder(enemy.config, enemy.settlement.tribe);
  return enemy;
}

static LevelMakerResult getLevelMaker(const ZLevelInfo& levelInfo, ResourceCounts resources, TribeId tribe,
    StairKey stairKey, ContentFactory* contentFactory) {
  return levelInfo.type.visit(
      [&](const WaterZLevel& level) {
        return LevelMakerResult{
            LevelMaker::getWaterZLevel(Random, level.waterType, levelInfo.width, level.creatures, stairKey),
            none, levelInfo.width
        };
      },
      [&](const FullZLevel& level) {
        optional<SettlementInfo> settlement;
        optional<EnemyInfo> enemy;
        if (level.enemy) {
          enemy = getEnemy(*level.enemy, contentFactory);
          settlement = enemy->settlement;
          CHECK(level.attackChance < 0.0001 || !!enemy->behaviour)
              << "Z-level enemy " << level.enemy->data() << " has positive attack chance, but no attack behaviour defined";
          if (Random.chance(level.attackChance)) {
            enemy->behaviour->triggers.push_back(Immediate{});
          }
        }
        return LevelMakerResult{
            LevelMaker::getFullZLevel(Random, settlement, resources, levelInfo.width, tribe, stairKey, &contentFactory->mapLayouts),
            std::move(enemy), levelInfo.width
        };
      });
}

LevelMakerResult getLevelMaker(RandomGen& random, ContentFactory* contentFactory, TribeAlignment alignment,
    int depth, TribeId tribe, StairKey stairKey) {
  auto& allLevels = contentFactory->zLevels;
  auto& resources = contentFactory->resources;
  auto zLevelGroup = [&] {
    switch (alignment) {
      case TribeAlignment::LAWFUL:
        return ZLevelGroup::LAWFUL;
      case TribeAlignment::EVIL:
        return ZLevelGroup::EVIL;
    }
  }();
  vector<ZLevelInfo> levels = concat<ZLevelInfo>({allLevels[ZLevelGroup::ALL], allLevels[zLevelGroup]});
  auto zLevel = *chooseZLevel(random, levels, depth);
  auto res = *chooseResourceCounts(random, resources, depth);
  return getLevelMaker(zLevel, res, tribe, stairKey, contentFactory);
}
