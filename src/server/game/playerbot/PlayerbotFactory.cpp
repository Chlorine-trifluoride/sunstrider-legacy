
#include "playerbot.h"
#include "PlayerbotFactory.h"
#include "GuildMgr.h"
#include "ItemTemplate.h"
#include "PlayerbotAIConfig.h"
//#include "DBCStore.h"
#include "SharedDefines.h"
//#include "../ahbot/AhBot.h"
#include "Pet.h"
#include "RandomPlayerbotFactory.h"


using namespace ai;
using namespace std;

uint32 PlayerbotFactory::tradeSkills[] =
{
    SKILL_ALCHEMY,
    SKILL_ENCHANTING,
    SKILL_SKINNING,
    SKILL_JEWELCRAFTING,
    //SKILL_INSCRIPTION,
    SKILL_TAILORING,
    SKILL_LEATHERWORKING,
    SKILL_ENGINEERING,
    SKILL_HERBALISM,
    SKILL_MINING,
    SKILL_BLACKSMITHING,
    SKILL_COOKING,
    SKILL_FIRST_AID,
    SKILL_FISHING
};

void PlayerbotFactory::Randomize()
{
    Randomize(true);
}

void PlayerbotFactory::Refresh()
{
    Prepare();
    InitEquipment(true);
    InitAmmo();
    InitFood();
    InitPotions();

    uint32 money = urand(level * 1000, level * 5 * 1000);
    if (bot->GetMoney() < money)
        bot->SetMoney(money);
    bot->SaveToDB();
}

void PlayerbotFactory::CleanRandomize()
{
    Randomize(false);
}

void PlayerbotFactory::Prepare()
{
    if (!itemQuality)
    {
        if (level <= 10)
            itemQuality = urand(ITEM_QUALITY_NORMAL, ITEM_QUALITY_UNCOMMON);
        else if (level <= 20)
            itemQuality = urand(ITEM_QUALITY_UNCOMMON, ITEM_QUALITY_RARE);
        else if (level <= 40)
            itemQuality = urand(ITEM_QUALITY_UNCOMMON, ITEM_QUALITY_EPIC);
        else if (level < 60)
            itemQuality = urand(ITEM_QUALITY_UNCOMMON, ITEM_QUALITY_EPIC);
        else
            itemQuality = urand(ITEM_QUALITY_RARE, ITEM_QUALITY_EPIC);
    }

    if (bot->IsDead())
        bot->ResurrectPlayer(1.0f, false);

    bot->CombatStop(true);
    bot->SetLevel(level);
    bot->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_HELM);
    bot->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_CLOAK);
}

void PlayerbotFactory::Randomize(bool incremental)
{
    Prepare();

    bot->ResetTalents(true);
    ClearSpells();
    ClearInventory();
    bot->SaveToDB();

    InitQuests();
    // quest rewards boost bot level, so reduce back
    bot->SetLevel(level);
    ClearInventory();
    bot->SetUInt32Value(PLAYER_XP, 0);
    CancelAuras();
    bot->SaveToDB();

    InitAvailableSpells();
    InitSkills();
    InitTradeSkills();
    InitTalents();
    InitAvailableSpells();
    InitSpecialSpells();
    InitMounts();
    UpdateTradeSkills();
    bot->SaveToDB();

    InitEquipment(incremental);
    InitBags();
    InitAmmo();
    InitFood();
    InitPotions();
    InitSecondEquipmentSet();
    InitInventory();
    InitGlyphs();
    InitGuild();
    bot->SetMoney(urand(level * 1000, level * 5 * 1000));
    bot->SaveToDB();

    InitPet();
    bot->SaveToDB();
}

void PlayerbotFactory::InitPet()
{
    Pet* pet = bot->GetPet();

    if (!pet)
    {
        if (bot->GetClass() != CLASS_HUNTER)
            return;

        Map* map = bot->FindMap();
        if (!map)
            return;

        vector<uint32> ids;
        CreatureTemplateContainer const& creatureTemplateContainer = sObjectMgr->GetCreatureTemplates();
        for (CreatureTemplateContainer::const_iterator i = creatureTemplateContainer.begin(); i != creatureTemplateContainer.end(); ++i)
        {
            CreatureTemplate const& co = i->second;
            if (!co.IsTameable(false))
                continue;

            if (co.minlevel > bot->GetLevel())
                continue;

            PetLevelInfo const* petInfo = sObjectMgr->GetPetLevelInfo(co.Entry, bot->GetLevel());
            if (!petInfo)
                continue;

            ids.push_back(i->first);
        }

        if (ids.empty())
        {
            sLog->outMessage("playerbot", LOG_LEVEL_ERROR, "No pets available for bot %s (%d level)", bot->GetName().c_str(), bot->GetLevel());
            return;
        }

        for (int i = 0; i < 100; i++)
        {
            int index = urand(0, ids.size() - 1);
            CreatureTemplate const* co = sObjectMgr->GetCreatureTemplate(ids[index]);

            PetLevelInfo const* petInfo = sObjectMgr->GetPetLevelInfo(co->Entry, bot->GetLevel());
            if (!petInfo)
                continue;

            uint32 guid = map->GenerateLowGuid<HighGuid::Pet>();
            pet = new Pet(bot, HUNTER_PET);
            if (!pet->Create(guid, map, 0, ids[index], 0))
            {
                delete pet;
                pet = NULL;
                continue;
            }

            pet->UpdatePosition(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetOrientation());
            pet->SetFaction(bot->GetFaction());
            pet->SetLevel(bot->GetLevel());
            bot->SetPetGUID(pet->GetGUID());
            bot->GetMap()->AddToMap(pet->ToCreature());
            bot->SetMinion(pet, true);
#ifdef LICH_KING
            pet->InitTalentForLevel();
#endif
            bot->PetSpellInitialize();
            bot->InitTamedPet(pet, bot->GetLevel(), 0);

            sLog->outMessage("playerbot", LOG_LEVEL_DEBUG,  "Bot %s: assign pet %d (%d level)", bot->GetName().c_str(), co->Entry, bot->GetLevel());
            pet->SavePetToDB(PET_SAVE_AS_CURRENT);
            break;
        }
    }
    if (!pet)
    {
        sLog->outMessage("playerbot", LOG_LEVEL_ERROR, "Cannot create pet for bot %s", bot->GetName().c_str());
        return;
    }

    for (PetSpellMap::const_iterator itr = pet->m_spells.begin(); itr != pet->m_spells.end(); ++itr)
    {
        if(itr->second.state == PETSPELL_REMOVED)
            continue;

        uint32 spellId = itr->first;
        const SpellInfo* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (spellInfo->IsPassive())
            continue;

        pet->ToggleAutocast(spellInfo, true);
    }
}

void PlayerbotFactory::ClearSpells()
{
    list<uint32> spells;
    for(PlayerSpellMap::iterator itr = bot->GetSpellMap().begin(); itr != bot->GetSpellMap().end(); ++itr)
    {
        uint32 spellId = itr->first;
        const SpellInfo* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if(itr->second->state == PLAYERSPELL_REMOVED || itr->second->disabled || spellInfo->IsPassive())
            continue;

        spells.push_back(spellId);
    }

    for (list<uint32>::iterator i = spells.begin(); i != spells.end(); ++i)
    {
        bot->RemoveSpell(*i, false/*TC , false*/);
    }
}

void PlayerbotFactory::InitSpells()
{
    for (int i = 0; i < 15; i++)
        InitAvailableSpells();
}

void PlayerbotFactory::InitTalents()
{
    uint32 point = urand(0, 100);
    uint8 cls = bot->GetClass();
    uint32 p1 = sPlayerbotAIConfig.specProbability[cls][0];
    uint32 p2 = p1 + sPlayerbotAIConfig.specProbability[cls][1];

    uint32 specNo = (point < p1 ? 0 : (point < p2 ? 1 : 2));
    InitTalents(specNo);

    if (bot->GetFreeTalentPoints())
        InitTalents(2 - specNo);
}


class DestroyItemsVisitor : public IterateItemsVisitor
{
public:
    DestroyItemsVisitor(Player* bot) : IterateItemsVisitor(), bot(bot) {}

    virtual bool Visit(Item* item)
    {
        uint32 id = item->GetTemplate()->ItemId;
        if (CanKeep(id))
        {
            keep.insert(id);
            return true;
        }

        bot->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
        return true;
    }

private:
    bool CanKeep(uint32 id)
    {
        if (keep.find(id) != keep.end())
            return false;

        if (sPlayerbotAIConfig.IsInRandomQuestItemList(id))
            return true;


        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(id);
        if (proto->Class == ITEM_CLASS_MISC && (proto->SubClass == ITEM_SUBCLASS_JUNK_REAGENT || proto->SubClass == ITEM_SUBCLASS_JUNK))
            return true;

        return false;
    }

private:
    Player* bot;
    set<uint32> keep;

};

bool PlayerbotFactory::CanEquipArmor(ItemTemplate const* proto)
{
    if (bot->HasSkill(SKILL_SHIELD) && proto->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD)
        return true;

    if (bot->HasSkill(SKILL_PLATE_MAIL))
    {
        if (proto->SubClass != ITEM_SUBCLASS_ARMOR_PLATE)
            return false;
    }
    else if (bot->HasSkill(SKILL_MAIL))
    {
        if (proto->SubClass != ITEM_SUBCLASS_ARMOR_MAIL)
            return false;
    }
    else if (bot->HasSkill(SKILL_LEATHER))
    {
        if (proto->SubClass != ITEM_SUBCLASS_ARMOR_LEATHER)
            return false;
    }

    if (proto->Quality <= ITEM_QUALITY_NORMAL)
        return true;

    uint8 sp = 0, ap = 0, tank = 0;
    for (int j = 0; j < MAX_ITEM_PROTO_STATS; ++j)
    {
        // for ItemStatValue != 0
        if(!proto->ItemStat[j].ItemStatValue)
            continue;

        AddItemStats(proto->ItemStat[j].ItemStatType, sp, ap, tank);
    }

    return CheckItemStats(sp, ap, tank);
}

bool PlayerbotFactory::CheckItemStats(uint8 sp, uint8 ap, uint8 tank)
{
    switch (bot->GetClass())
    {
    case CLASS_PRIEST:
    case CLASS_MAGE:
    case CLASS_WARLOCK:
        if (!sp || ap > sp || tank > sp)
            return false;
        break;
    case CLASS_PALADIN:
    case CLASS_WARRIOR:
        if ((!ap && !tank) || sp > ap || sp > tank)
            return false;
        break;
    case CLASS_HUNTER:
    case CLASS_ROGUE:
        if (!ap || sp > ap || sp > tank)
            return false;
        break;
    }

    return sp || ap || tank;
}

void PlayerbotFactory::AddItemStats(uint32 mod, uint8 &sp, uint8 &ap, uint8 &tank)
{
    switch (mod)
    {
    case ITEM_MOD_HIT_RATING:
    case ITEM_MOD_CRIT_RATING:
    case ITEM_MOD_HASTE_RATING:
    case ITEM_MOD_HEALTH:
    case ITEM_MOD_STAMINA:
    case ITEM_MOD_HEALTH_REGEN:
    case ITEM_MOD_MANA:
    case ITEM_MOD_INTELLECT:
    case ITEM_MOD_SPIRIT:
    case ITEM_MOD_MANA_REGENERATION:
    case ITEM_MOD_SPELL_POWER:
    case ITEM_MOD_SPELL_PENETRATION:
    case ITEM_MOD_HIT_SPELL_RATING:
    case ITEM_MOD_CRIT_SPELL_RATING:
    case ITEM_MOD_HASTE_SPELL_RATING:
        sp++;
        break;
    }

    switch (mod)
    {
    case ITEM_MOD_HIT_RATING:
    case ITEM_MOD_CRIT_RATING:
    case ITEM_MOD_HASTE_RATING:
    case ITEM_MOD_AGILITY:
    case ITEM_MOD_STRENGTH:
    case ITEM_MOD_HEALTH:
    case ITEM_MOD_STAMINA:
    case ITEM_MOD_HEALTH_REGEN:
    case ITEM_MOD_DEFENSE_SKILL_RATING:
    case ITEM_MOD_DODGE_RATING:
    case ITEM_MOD_PARRY_RATING:
    case ITEM_MOD_BLOCK_RATING:
    case ITEM_MOD_HIT_TAKEN_MELEE_RATING:
    case ITEM_MOD_HIT_TAKEN_RANGED_RATING:
    case ITEM_MOD_HIT_TAKEN_SPELL_RATING:
    case ITEM_MOD_CRIT_TAKEN_MELEE_RATING:
    case ITEM_MOD_CRIT_TAKEN_RANGED_RATING:
    case ITEM_MOD_CRIT_TAKEN_SPELL_RATING:
    case ITEM_MOD_HIT_TAKEN_RATING:
    case ITEM_MOD_CRIT_TAKEN_RATING:
    case ITEM_MOD_RESILIENCE_RATING:
    case ITEM_MOD_BLOCK_VALUE:
        tank++;
        break;
    }

    switch (mod)
    {
    case ITEM_MOD_HEALTH:
    case ITEM_MOD_STAMINA:
    case ITEM_MOD_HEALTH_REGEN:
    case ITEM_MOD_AGILITY:
    case ITEM_MOD_STRENGTH:
    case ITEM_MOD_HIT_MELEE_RATING:
    case ITEM_MOD_HIT_RANGED_RATING:
    case ITEM_MOD_CRIT_MELEE_RATING:
    case ITEM_MOD_CRIT_RANGED_RATING:
    case ITEM_MOD_HASTE_MELEE_RATING:
    case ITEM_MOD_HASTE_RANGED_RATING:
    case ITEM_MOD_HIT_RATING:
    case ITEM_MOD_CRIT_RATING:
    case ITEM_MOD_HASTE_RATING:
    case ITEM_MOD_EXPERTISE_RATING:
    case ITEM_MOD_ATTACK_POWER:
    case ITEM_MOD_RANGED_ATTACK_POWER:
    case ITEM_MOD_ARMOR_PENETRATION_RATING:
        ap++;
        break;
    }
}

bool PlayerbotFactory::CanEquipWeapon(ItemTemplate const* proto)
{
    switch (bot->GetClass())
    {
    case CLASS_PRIEST:
        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_STAFF &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_WAND &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE)
            return false;
        break;
    case CLASS_MAGE:
    case CLASS_WARLOCK:
        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_STAFF &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_WAND &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD)
            return false;
        break;
    case CLASS_WARRIOR:
        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_GUN &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_CROSSBOW &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_BOW &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_THROWN)
            return false;
        break;
    case CLASS_PALADIN:
        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD)
            return false;
        break;
    case CLASS_SHAMAN:
        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_STAFF)
            return false;
        break;
    case CLASS_DRUID:
        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_DAGGER &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_STAFF)
            return false;
        break;
    case CLASS_HUNTER:
        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_AXE2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_GUN &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_CROSSBOW &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_BOW)
            return false;
        break;
    case CLASS_ROGUE:
        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_DAGGER &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_GUN &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_CROSSBOW &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_BOW &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_THROWN)
            return false;
        break;
    }

    return true;
}

bool PlayerbotFactory::CanEquipItem(ItemTemplate const* proto, uint32 desiredQuality)
{
    if (proto->Duration & 0x80000000)
        return false;

    if (proto->Quality != desiredQuality)
        return false;

    if (proto->Bonding == BIND_QUEST_ITEM || proto->Bonding == BIND_WHEN_USE)
        return false;

    if (proto->Class == ITEM_CLASS_CONTAINER)
        return true;

    uint32 requiredLevel = proto->RequiredLevel;
    if (!requiredLevel)
        return false;

    uint32 _level = bot->GetLevel();
    uint32 delta = 2;
    if (_level < 15)
        delta = urand(7, 15);
    else if (proto->Class == ITEM_CLASS_WEAPON || proto->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD)
        delta = urand(2, 3);
    else if (!(_level % 10) || (_level % 10) == 9)
        delta = 2;
    else if (_level < 40)
        delta = urand(5, 10);
    else if (_level < 60)
        delta = urand(3, 7);
    else if (_level < 70)
        delta = urand(2, 5);
    else if (_level < 80)
        delta = urand(2, 4);

    if (desiredQuality > ITEM_QUALITY_NORMAL &&
            (requiredLevel > _level || requiredLevel < _level - delta))
        return false;

    for (uint32 gap = 60; gap <= 80; gap += 10)
    {
        if (_level > gap && requiredLevel <= gap)
            return false;
    }

    return true;
}

void PlayerbotFactory::InitEquipment(bool incremental)
{
    DestroyItemsVisitor visitor(bot);
    IterateItems(&visitor, ITERATE_ALL_ITEMS);

    map<uint8, vector<uint32> > items;
    for(uint8 slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (slot == EQUIPMENT_SLOT_TABARD || slot == EQUIPMENT_SLOT_BODY)
            continue;

        uint32 desiredQuality = itemQuality;
        if (urand(0, 100) < 100 * sPlayerbotAIConfig.randomGearLoweringChance && desiredQuality > ITEM_QUALITY_NORMAL) {
            desiredQuality--;
        }

        do
        {
            ItemTemplateContainer const& itemTemplates = sObjectMgr->GetItemTemplateStore();
            for (ItemTemplateContainer::const_iterator i = itemTemplates.begin(); i != itemTemplates.end(); ++i)
            {
                uint32 itemId = i->first;
                ItemTemplate const* proto = &i->second;

                if (proto->Class != ITEM_CLASS_WEAPON &&
                    proto->Class != ITEM_CLASS_ARMOR &&
                    proto->Class != ITEM_CLASS_CONTAINER &&
                    proto->Class != ITEM_CLASS_PROJECTILE)
                    continue;

                if (!CanEquipItem(proto, desiredQuality))
                    continue;

                if (proto->Class == ITEM_CLASS_ARMOR && (
                    slot == EQUIPMENT_SLOT_HEAD ||
                    slot == EQUIPMENT_SLOT_SHOULDERS ||
                    slot == EQUIPMENT_SLOT_CHEST ||
                    slot == EQUIPMENT_SLOT_WAIST ||
                    slot == EQUIPMENT_SLOT_LEGS ||
                    slot == EQUIPMENT_SLOT_FEET ||
                    slot == EQUIPMENT_SLOT_WRISTS ||
                    slot == EQUIPMENT_SLOT_HANDS) && !CanEquipArmor(proto))
                        continue;

                if (proto->Class == ITEM_CLASS_WEAPON && !CanEquipWeapon(proto))
                    continue;

                if (slot == EQUIPMENT_SLOT_OFFHAND && bot->GetClass() == CLASS_ROGUE && proto->Class != ITEM_CLASS_WEAPON)
                    continue;

                uint16 dest = 0;
                if (CanEquipUnseenItem(slot, dest, itemId))
                    items[slot].push_back(itemId);
            }
        } while (items[slot].empty() && desiredQuality-- > ITEM_QUALITY_NORMAL);
    }

    for(uint8 slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (slot == EQUIPMENT_SLOT_TABARD || slot == EQUIPMENT_SLOT_BODY)
            continue;

        vector<uint32>& ids = items[slot];
        if (ids.empty())
        {
            sLog->outMessage("playerbot", LOG_LEVEL_DEBUG,  "%s: no items to equip for slot %d", bot->GetName().c_str(), slot);
            continue;
        }

        for (int attempts = 0; attempts < 15; attempts++)
        {
            uint32 index = urand(0, ids.size() - 1);
            uint32 newItemId = ids[index];
            Item* oldItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);

            if (incremental && !IsDesiredReplacement(oldItem)) {
                continue;
            }

            uint16 dest;
            if (!CanEquipUnseenItem(slot, dest, newItemId))
                continue;

            if (oldItem)
            {
                bot->RemoveItem(INVENTORY_SLOT_BAG_0, slot, true);
                oldItem->DestroyForPlayer(bot, false);
            }

            Item* newItem = bot->EquipNewItem(dest, newItemId, true);
            if (newItem)
            {
                newItem->AddToWorld();
                newItem->AddItemToUpdateQueueOf(bot);
                bot->AutoUnequipOffhandIfNeed();
                EnchantItem(newItem);
                break;
            }
        }
    }
}

bool PlayerbotFactory::IsDesiredReplacement(Item* item)
{
    if (!item)
        return true;

    ItemTemplate const* proto = item->GetTemplate();
    int delta = 1 + (80 - bot->GetLevel()) / 10;
    return (int)bot->GetLevel() - (int)proto->RequiredLevel > delta;
}

void PlayerbotFactory::InitSecondEquipmentSet()
{
    if (bot->GetClass() == CLASS_MAGE || bot->GetClass() == CLASS_WARLOCK || bot->GetClass() == CLASS_PRIEST)
        return;

    map<uint32, vector<uint32> > items;

    uint32 desiredQuality = itemQuality;
    while (urand(0, 100) < 100 * sPlayerbotAIConfig.randomGearLoweringChance && desiredQuality > ITEM_QUALITY_NORMAL) {
        desiredQuality--;
    }

    do
    {
        ItemTemplateContainer const& itemTemplates = sObjectMgr->GetItemTemplateStore();
        for (ItemTemplateContainer::const_iterator i = itemTemplates.begin(); i != itemTemplates.end(); ++i)
        {
            uint32 itemId = i->first;
            ItemTemplate const* proto = &i->second;
            if (!proto)
                continue;

            if (!CanEquipItem(proto, desiredQuality))
                continue;

            if (proto->Class == ITEM_CLASS_WEAPON)
            {
                if (!CanEquipWeapon(proto))
                    continue;

                Item* existingItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
                if (existingItem)
                {
                    switch (existingItem->GetTemplate()->SubClass)
                    {
                    case ITEM_SUBCLASS_WEAPON_AXE:
                    case ITEM_SUBCLASS_WEAPON_DAGGER:
                    case ITEM_SUBCLASS_WEAPON_FIST:
                    case ITEM_SUBCLASS_WEAPON_MACE:
                    case ITEM_SUBCLASS_WEAPON_SWORD:
                        if (proto->SubClass == ITEM_SUBCLASS_WEAPON_AXE || proto->SubClass == ITEM_SUBCLASS_WEAPON_DAGGER ||
                            proto->SubClass == ITEM_SUBCLASS_WEAPON_FIST || proto->SubClass == ITEM_SUBCLASS_WEAPON_MACE ||
                            proto->SubClass == ITEM_SUBCLASS_WEAPON_SWORD)
                            continue;
                        break;
                    default:
                        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_AXE && proto->SubClass != ITEM_SUBCLASS_WEAPON_DAGGER &&
                            proto->SubClass != ITEM_SUBCLASS_WEAPON_FIST && proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE &&
                            proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD)
                            continue;
                        break;
                    }
                }
            }
            else if (proto->Class == ITEM_CLASS_ARMOR && proto->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD)
            {
                if (!CanEquipArmor(proto))
                    continue;

                Item* existingItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
                if (existingItem && existingItem->GetTemplate()->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD)
                    continue;
            }
            else
                continue;

            items[proto->Class].push_back(itemId);
        }
    } while (items[ITEM_CLASS_ARMOR].empty() && items[ITEM_CLASS_WEAPON].empty() && desiredQuality-- > ITEM_QUALITY_NORMAL);

    for (map<uint32, vector<uint32> >::iterator i = items.begin(); i != items.end(); ++i)
    {
        vector<uint32>& ids = i->second;
        if (ids.empty())
        {
            sLog->outMessage("playerbot", LOG_LEVEL_DEBUG,  "%s: no items to make second equipment set for slot %d", bot->GetName().c_str(), i->first);
            continue;
        }

        for (int attempts = 0; attempts < 15; attempts++)
        {
            uint32 index = urand(0, ids.size() - 1);
            uint32 newItemId = ids[index];

            ItemPosCountVec sDest;
            Item* newItem = StoreItem(newItemId, 1);
            if (newItem)
            {
                EnchantItem(newItem);
                newItem->AddToWorld();
                newItem->AddItemToUpdateQueueOf(bot);
                break;
            }
        }
    }
}

void PlayerbotFactory::InitBags()
{
    vector<uint32> ids;

    ItemTemplateContainer const& itemTemplates = sObjectMgr->GetItemTemplateStore();
    for (ItemTemplateContainer::const_iterator i = itemTemplates.begin(); i != itemTemplates.end(); ++i)
    {
        uint32 itemId = i->first;
        ItemTemplate const* proto = &i->second;
        if (proto->Class != ITEM_CLASS_CONTAINER)
            continue;

        if (!CanEquipItem(proto, ITEM_QUALITY_NORMAL))
            continue;

        ids.push_back(itemId);
    }

    if (ids.empty())
    {
        sLog->outMessage("playerbot", LOG_LEVEL_ERROR, "%s: no bags found", bot->GetName().c_str());
        return;
    }

    for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
    {
        for (int attempts = 0; attempts < 15; attempts++)
        {
            uint32 index = urand(0, ids.size() - 1);
            uint32 newItemId = ids[index];

            uint16 dest;
            if (!CanEquipUnseenItem(slot, dest, newItemId))
                continue;

            Item* newItem = bot->EquipNewItem(dest, newItemId, true);
            if (newItem)
            {
                newItem->AddToWorld();
                newItem->AddItemToUpdateQueueOf(bot);
                break;
            }
        }
    }
}

void PlayerbotFactory::EnchantItem(Item* item)
{
    if (urand(0, 100) < 100 * sPlayerbotAIConfig.randomGearLoweringChance)
        return;

    if (bot->GetLevel() < urand(40, 50))
        return;

    ItemTemplate const* proto = item->GetTemplate();
    int32 itemLevel = proto->ItemLevel;

    vector<uint32> ids;
    auto spellStore = sObjectMgr->GetSpellStore();
    for (auto spellitr : spellStore)
    {
        uint32 spellId = spellitr.first;
        SpellInfo const *entry = sSpellMgr->GetSpellInfo(spellId);
        if (!entry)
            continue;

        int32 requiredLevel = (int32)entry->BaseLevel;
        if (requiredLevel && (requiredLevel > itemLevel || requiredLevel < itemLevel - 35))
            continue;

        if (entry->MaxLevel && level > entry->MaxLevel)
            continue;

        uint32 spellLevel = entry->SpellLevel;
        if (spellLevel && (spellLevel > level || spellLevel < level - 10))
            continue;

        for (int j = 0; j < MAX_SPELL_EFFECTS; ++j)
        {
            if (entry->Effects[j].Effect != SPELL_EFFECT_ENCHANT_ITEM)
                continue;

            uint32 enchant_id = entry->Effects[j].MiscValue;
            if (!enchant_id)
                continue;

            SpellItemEnchantmentEntry const* enchant = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
            if (!enchant || enchant->slot != PERM_ENCHANTMENT_SLOT)
                continue;
#ifdef LICH_KING
            if (enchant->requiredLevel && enchant->requiredLevel > level)
                continue;
#endif

            uint8 sp = 0, ap = 0, tank = 0;
            for (int i = 0; i < 3; ++i)
            {
                if (enchant->type[i] != ITEM_ENCHANTMENT_TYPE_STAT)
                    continue;

                AddItemStats(enchant->spellid[i], sp, ap, tank);
            }

            if (!CheckItemStats(sp, ap, tank))
                continue;

            if (enchant->EnchantmentCondition && !bot->EnchantmentFitsRequirements(enchant->EnchantmentCondition, -1))
                continue;

            if (!item->IsFitToSpellRequirements(entry))
                continue;

            ids.push_back(enchant_id);
        }
    }

    if (ids.empty())
    {
        sLog->outMessage("playerbot", LOG_LEVEL_DEBUG,  "%s: no enchantments found for item %d", bot->GetName().c_str(), item->GetTemplate()->ItemId);
        return;
    }

    int index = urand(0, ids.size() - 1);
    uint32 id = ids[index];

    SpellItemEnchantmentEntry const* enchant = sSpellItemEnchantmentStore.LookupEntry(id);
    if (!enchant)
        return;

    bot->ApplyEnchantment(item, PERM_ENCHANTMENT_SLOT, false);
    item->SetEnchantment(PERM_ENCHANTMENT_SLOT, id, 0, 0);
    bot->ApplyEnchantment(item, PERM_ENCHANTMENT_SLOT, true);
}

bool PlayerbotFactory::CanEquipUnseenItem(uint8 slot, uint16 &dest, uint32 item)
{
    dest = 0;
    Item *pItem = Item::CreateItem(item, 1, bot);
    if (pItem)
    {
        InventoryResult result = bot->CanEquipItem(slot, dest, pItem, true, false);
        pItem->RemoveItemFromUpdateQueueOf(bot);
        delete pItem;
        return result == EQUIP_ERR_OK;
    }

    return false;
}

void PlayerbotFactory::InitTradeSkills()
{
    for (int i = 0; i < sizeof(tradeSkills) / sizeof(uint32); ++i)
    {
        bot->SetSkill(tradeSkills[i], 0, 0, 0);
    }

    vector<uint32> firstSkills;
    vector<uint32> secondSkills;
    switch (bot->GetClass())
    {
    case CLASS_WARRIOR:
    case CLASS_PALADIN:
        firstSkills.push_back(SKILL_MINING);
        secondSkills.push_back(SKILL_BLACKSMITHING);
        secondSkills.push_back(SKILL_ENGINEERING);
        break;
    case CLASS_SHAMAN:
    case CLASS_DRUID:
    case CLASS_HUNTER:
    case CLASS_ROGUE:
        firstSkills.push_back(SKILL_SKINNING);
        secondSkills.push_back(SKILL_LEATHERWORKING);
        break;
    default:
        firstSkills.push_back(SKILL_TAILORING);
        secondSkills.push_back(SKILL_ENCHANTING);
    }

    SetRandomSkill(SKILL_FIRST_AID);
    SetRandomSkill(SKILL_FISHING);
    SetRandomSkill(SKILL_COOKING);

    switch (urand(0, 3))
    {
    case 0:
        SetRandomSkill(SKILL_HERBALISM);
        SetRandomSkill(SKILL_ALCHEMY);
        break;
    case 1:
        SetRandomSkill(SKILL_HERBALISM);
#ifdef LICH_KING
        SetRandomSkill(SKILL_INSCRIPTION);
#endif
        break;
    case 2:
        SetRandomSkill(SKILL_MINING);
        SetRandomSkill(SKILL_JEWELCRAFTING);
        break;
    case 3:
        SetRandomSkill(firstSkills[urand(0, firstSkills.size() - 1)]);
        SetRandomSkill(secondSkills[urand(0, secondSkills.size() - 1)]);
        break;
    }
}

void PlayerbotFactory::UpdateTradeSkills()
{
    for (int i = 0; i < sizeof(tradeSkills) / sizeof(uint32); ++i)
    {
        if (bot->GetSkillValue(tradeSkills[i]) == 1)
            bot->SetSkill(tradeSkills[i], 0, 0, 0);
    }
}

void PlayerbotFactory::InitSkills()
{
    //uint32 maxValue = level * 5;
    SetRandomSkill(SKILL_DEFENSE);
    SetRandomSkill(SKILL_SWORDS);
    SetRandomSkill(SKILL_AXES);
    SetRandomSkill(SKILL_BOWS);
    SetRandomSkill(SKILL_GUNS);
    SetRandomSkill(SKILL_MACES);
    SetRandomSkill(SKILL_2H_SWORDS);
    SetRandomSkill(SKILL_STAVES);
    SetRandomSkill(SKILL_2H_MACES);
    SetRandomSkill(SKILL_2H_AXES);
    SetRandomSkill(SKILL_DAGGERS);
    SetRandomSkill(SKILL_THROWN);
    SetRandomSkill(SKILL_CROSSBOWS);
    SetRandomSkill(SKILL_WANDS);
    SetRandomSkill(SKILL_POLEARMS);
    SetRandomSkill(SKILL_FIST_WEAPONS);

    if (bot->GetLevel() >= 70)
        bot->SetSkill(SKILL_RIDING, 0, 300, 300);
    else if (bot->GetLevel() >= 60)
        bot->SetSkill(SKILL_RIDING, 0, 225, 225);
    else if (bot->GetLevel() >= 40)
        bot->SetSkill(SKILL_RIDING, 0, 150, 150);
    else if (bot->GetLevel() >= 20)
        bot->SetSkill(SKILL_RIDING, 0, 75, 75);
    else
        bot->SetSkill(SKILL_RIDING, 0, 0, 0);

    uint32 skillLevel = bot->GetLevel() < 40 ? 0 : 1;
    switch (bot->GetClass())
    {
    case CLASS_DEATH_KNIGHT:
    case CLASS_WARRIOR:
    case CLASS_PALADIN:
        bot->SetSkill(SKILL_PLATE_MAIL, 0, skillLevel, skillLevel);
        break;
    case CLASS_SHAMAN:
    case CLASS_HUNTER:
        bot->SetSkill(SKILL_MAIL, 0, skillLevel, skillLevel);
    }
}

void PlayerbotFactory::SetRandomSkill(uint16 id)
{
    uint32 maxValue = level * 5;
    uint32 curValue = urand(maxValue - level, maxValue);
    bot->SetSkill(id, 0, curValue, maxValue);

}

void PlayerbotFactory::InitAvailableSpells()
{
    /* old code

    CreatureTemplateContainer const& creatureTemplateContainer = sObjectMgr->GetCreatureTemplates();
    for (CreatureTemplateContainer::const_iterator i = creatureTemplateContainer->begin(); i != creatureTemplateContainer->end(); ++i)
    {
        CreatureTemplate const& co = i->second;
        if (co.trainer_type != TRAINER_TYPE_TRADESKILLS && co.trainer_type != TRAINER_TYPE_CLASS)
            continue;

        if (co.trainer_type == TRAINER_TYPE_CLASS && co.trainer_class != bot->GetClass())
            continue;

        uint32 trainerId = co.Entry;

        TrainerSpellData const* trainer_spells = sObjectMgr->GetNpcTrainerSpells(trainerId);
        if (!trainer_spells)
            trainer_spells = sObjectMgr->GetNpcTrainerSpells(trainerId);

        if (!trainer_spells)
            continue;

        for (auto itr =  trainer_spells->spellList.begin(); itr !=  trainer_spells->spellList.end(); ++itr)
        {
            TrainerSpell const* tSpell = &itr->second;

            if (!tSpell)
                continue;

            if (!tSpell->learnedSpell[0] && !bot->IsSpellFitByClassAndRace(tSpell->learnedSpell[0]))
                continue;

            TrainerSpellState state = bot->GetTrainerSpellState(tSpell);
            if (state != TRAINER_SPELL_GREEN)
                continue;

            if (tSpell->learnedSpell)
                bot->LearnSpell(tSpell->learnedSpell[0], false);
            else
                ai->CastSpell(tSpell->spell, bot);
        }
    }
    */
    //sunstrider code instead. Previous code is horribly slow
    bot->LearnDefaultSkills();
    bot->LearnDefaultSpells();
    bot->LearnAllClassProficiencies();
    bot->LearnAllClassSpells();
}

void PlayerbotFactory::InitSpecialSpells()
{
    for (list<uint32>::iterator i = sPlayerbotAIConfig.randomBotSpellIds.begin(); i != sPlayerbotAIConfig.randomBotSpellIds.end(); ++i)
    {
        uint32 spellId = *i;
        bot->LearnSpell(spellId, TRIGGERED_NONE);
    }
}

void PlayerbotFactory::InitTalents(uint32 specNo)
{
    uint32 classMask = bot->GetClassMask();
    map<uint32, vector<TalentEntry const*> > spells;
    for (uint32 i = 0; i < sTalentStore.GetNumRows(); ++i)
    {
        TalentEntry const *talentInfo = sTalentStore.LookupEntry(i);
        if(!talentInfo)
            continue;

        TalentTabEntry const *talentTabInfo = sTalentTabStore.LookupEntry( talentInfo->TalentTab );
        if(!talentTabInfo || talentTabInfo->tabpage != specNo)
            continue;

        if( (classMask & talentTabInfo->ClassMask) == 0 )
            continue;

        spells[talentInfo->Row].push_back(talentInfo);
    }

    uint32 freePoints = bot->GetFreeTalentPoints();
    for (map<uint32, vector<TalentEntry const*> >::iterator i = spells.begin(); i != spells.end(); ++i)
    {
        vector<TalentEntry const*>& _spells = i->second;
        if (_spells.empty())
        {
            sLog->outMessage("playerbot", LOG_LEVEL_ERROR, "%s: No spells for talent row %d", bot->GetName().c_str(), i->first);
            continue;
        }

        int attemptCount = 0;
        while (!_spells.empty() && (int)freePoints - (int)bot->GetFreeTalentPoints() < 5 && attemptCount++ < 3 && bot->GetFreeTalentPoints())
        {
            int index = urand(0, _spells.size() - 1);
            TalentEntry const *talentInfo = _spells[index];
            int maxRank = 0;
            for (int rank = 0; rank < min((uint32)MAX_TALENT_RANK, bot->GetFreeTalentPoints()); ++rank)
            {
                uint32 spellId = talentInfo->RankID[rank];
                if (!spellId)
                    continue;

                maxRank = rank;
            }

            bot->LearnTalent(talentInfo->TalentID, maxRank);
            _spells.erase(_spells.begin() + index);
        }

        freePoints = bot->GetFreeTalentPoints();
    }

#ifdef LICH_KING
    for (uint32 i = 0; i < MAX_TALENT_SPECS; ++i)
    {
        for (PlayerTalentMap::iterator itr = bot->GetTalentMap(i).begin(); itr != bot->GetTalentMap(i).end(); ++itr)
        {
            if (itr->second->state != PLAYERSPELL_REMOVED)
                itr->second->state = PLAYERSPELL_CHANGED;
        }
    }
#endif
}

ObjectGuid PlayerbotFactory::GetRandomBot()
{
    vector<ObjectGuid> guids;
    for (list<uint32>::iterator i = sPlayerbotAIConfig.randomBotAccounts.begin(); i != sPlayerbotAIConfig.randomBotAccounts.end(); i++)
    {
        uint32 accountId = *i;
        if (!sAccountMgr->GetCharactersCount(accountId))
            continue;

        QueryResult result = CharacterDatabase.PQuery("SELECT guid FROM characters WHERE account = '%u'", accountId);
        if (!result)
            continue;

        do
        {
            Field* fields = result->Fetch();
            ObjectGuid guid = ObjectGuid(HighGuid::Player, fields[0].GetUInt32());
            if (!ObjectAccessor::FindPlayer(guid))
                guids.push_back(guid);
        } while (result->NextRow());
    }

    if (guids.empty())
        return ObjectGuid();

    int index = urand(0, guids.size() - 1);
    return guids[index];
}

void PlayerbotFactory::InitQuests()
{
    /* TC QueryResult results = WorldDatabase.PQuery("SELECT Id, RequiredClasses, RequiredRaces FROM quest_template where Level = -1 and MinLevel <= '%u'",
    Replaced original code to avoid unecessary db query
    */

    //uint32 botLevel = bot->GetLevel();
    /* sunstrider: Commented out since it takes forever...
    auto& quests = sObjectMgr->GetQuestTemplates();
    for (auto itr : quests)
    {
        uint32 questId = itr.first;
        auto questInfo = itr.second;
        if (questInfo->IsDaily() || questInfo->IsWeekly() || questInfo->IsRepeatable() || questInfo->IsMonthly())
            continue;
        if (!bot->CanTakeQuest(questInfo, false))
            continue;

        bot->SetQuestStatus(questId, QUEST_STATUS_COMPLETE);
        bot->RewardQuest(questInfo, 0, bot, false);
        ClearInventory();
    }
    */
}

void PlayerbotFactory::ClearInventory()
{
    DestroyItemsVisitor visitor(bot);
    IterateItems(&visitor);
}

void PlayerbotFactory::InitAmmo()
{
    if (bot->GetClass() != CLASS_HUNTER && bot->GetClass() != CLASS_ROGUE && bot->GetClass() != CLASS_WARRIOR)
        return;

    Item* const pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
    if (!pItem)
        return;

    uint32 subClass = 0;
    switch (pItem->GetTemplate()->SubClass)
    {
    case ITEM_SUBCLASS_WEAPON_GUN:
        subClass = ITEM_SUBCLASS_BULLET;
        break;
    case ITEM_SUBCLASS_WEAPON_BOW:
    case ITEM_SUBCLASS_WEAPON_CROSSBOW:
        subClass = ITEM_SUBCLASS_ARROW;
        break;
    }

    if (!subClass)
        return;

    QueryResult results = WorldDatabase.PQuery("select max(entry), max(RequiredLevel) from item_template where class = '%u' and subclass = '%u' and RequiredLevel <= '%u'",
            ITEM_CLASS_PROJECTILE, subClass, bot->GetLevel());

    Field* fields = results->Fetch();
    if (fields)
    {
        uint32 entry = fields[0].GetUInt32();
        for (int i = 0; i < 5; i++)
        {
            bot->StoreNewItemInBestSlots(entry, 1000);
        }
        bot->SetAmmo(entry);
    }
}

void PlayerbotFactory::InitMounts()
{
    map<uint32, map<int32, vector<uint32> > > allSpells;

    auto spellStore = sObjectMgr->GetSpellStore();
    for (auto spellItr : spellStore)
    {
        uint32 spellId = spellItr.first;
        SpellInfo const *spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo || spellInfo->Effects[0].ApplyAuraName != SPELL_AURA_MOUNTED)
            continue;

        if (spellInfo->GetDuration() != -1)
            continue;

        int32 effect = max(spellInfo->Effects[1].BasePoints, spellInfo->Effects[2].BasePoints);
        if (effect < 50)
            continue;

        uint32 index = (spellInfo->Effects[1].ApplyAuraName == SPELL_AURA_MOD_MOUNTED_FLIGHT_SPEED_ALWAYS ||
                spellInfo->Effects[2].ApplyAuraName == SPELL_AURA_MOD_MOUNTED_FLIGHT_SPEED_ALWAYS) ? 1 : 0;
        allSpells[index][effect].push_back(spellId);
    }

    for (uint32 type = 0; type < 2; ++type)
    {
        map<int32, vector<uint32> >& spells = allSpells[type];
        for (map<int32, vector<uint32> >::iterator i = spells.begin(); i != spells.end(); ++i)
        {
//            int32 effect = i->first;
            vector<uint32>& ids = i->second;
            uint32 index = urand(0, ids.size() - 1);
            if (index >= ids.size())
                continue;

            bot->LearnSpell(ids[index], false);
        }
    }
}

void PlayerbotFactory::InitPotions()
{
    map<uint32, vector<uint32>> items;
    ItemTemplateContainer const& itemTemplateContainer = sObjectMgr->GetItemTemplateStore();
    for (ItemTemplateContainer::const_iterator i = itemTemplateContainer.begin(); i != itemTemplateContainer.end(); ++i)
    {
//        ItemTemplate const& itemTemplate = i->second;
        uint32 itemId = i->first;
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        if (!proto)
            continue;

        if (proto->Class != ITEM_CLASS_CONSUMABLE ||
            proto->SubClass != ITEM_SUBCLASS_POTION ||
            proto->Spells[0].SpellCategory != 4 ||
            proto->Bonding != NO_BIND)
            continue;

        if (proto->RequiredLevel > bot->GetLevel() || proto->RequiredLevel < bot->GetLevel() - 10)
            continue;

        if (proto->RequiredSkill && !bot->HasSkill(proto->RequiredSkill))
            continue;

        if (proto->Area || proto->Map || proto->RequiredCityRank || proto->RequiredHonorRank)
            continue;

        for (int j = 0; j < MAX_ITEM_PROTO_SPELLS; j++)
        {
            const SpellInfo* const spellInfo = sSpellMgr->GetSpellInfo(proto->Spells[j].SpellId);
            if (!spellInfo)
                continue;

            for (int k = 0 ; k < 3; k++)
            {
                if (spellInfo->Effects[k].Effect == SPELL_EFFECT_HEAL || spellInfo->Effects[k].Effect == SPELL_EFFECT_ENERGIZE)
                {
                    items[spellInfo->Effects[k].Effect].push_back(itemId);
                    break;
                }
            }
        }
    }

    uint32 effects[] = { SPELL_EFFECT_HEAL, SPELL_EFFECT_ENERGIZE };
    for (int i = 0; i < sizeof(effects) / sizeof(uint32); ++i)
    {
        uint32 effect = effects[i];
        vector<uint32>& ids = items[effect];
        uint32 index = urand(0, ids.size() - 1);
        if (index >= ids.size())
            continue;

        uint32 itemId = ids[index];
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        bot->StoreNewItemInBestSlots(itemId, urand(1, proto->GetMaxStackSize()));
   }
}

void PlayerbotFactory::InitFood()
{
    map<uint32, vector<uint32> > items;
    ItemTemplateContainer const& itemTemplateContainer = sObjectMgr->GetItemTemplateStore();
    for (ItemTemplateContainer::const_iterator i = itemTemplateContainer.begin(); i != itemTemplateContainer.end(); ++i)
    {
//        ItemTemplate const& itemTemplate = i->second;
        uint32 itemId = i->first;
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        if (!proto)
            continue;

        if (proto->Class != ITEM_CLASS_CONSUMABLE ||
            proto->SubClass != ITEM_SUBCLASS_FOOD ||
            (proto->Spells[0].SpellCategory != 11 && proto->Spells[0].SpellCategory != 59) ||
            proto->Bonding != NO_BIND)
            continue;

        if (proto->RequiredLevel > bot->GetLevel() || proto->RequiredLevel < bot->GetLevel() - 10)
            continue;

        if (proto->RequiredSkill && !bot->HasSkill(proto->RequiredSkill))
            continue;

        if (proto->Area || proto->Map || proto->RequiredCityRank || proto->RequiredHonorRank)
            continue;

        items[proto->Spells[0].SpellCategory].push_back(itemId);
    }

    uint32 categories[] = { 11, 59 };
    for (int i = 0; i < sizeof(categories) / sizeof(uint32); ++i)
    {
        uint32 category = categories[i];
        vector<uint32>& ids = items[category];
        uint32 index = urand(0, ids.size() - 1);
        if (index >= ids.size())
            continue;

        uint32 itemId = ids[index];
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        bot->StoreNewItemInBestSlots(itemId, urand(1, proto->GetMaxStackSize()));
   }
}


void PlayerbotFactory::CancelAuras()
{
    bot->RemoveAllAuras();
}

void PlayerbotFactory::InitInventory()
{
    InitInventoryTrade();
    InitInventoryEquip();
    InitInventorySkill();
}

void PlayerbotFactory::InitInventorySkill()
{
    if (bot->HasSkill(SKILL_MINING)) {
        StoreItem(2901, 1); // Mining Pick
    }
    if (bot->HasSkill(SKILL_JEWELCRAFTING)) {
        StoreItem(20815, 1); // Jeweler's Kit
        StoreItem(20824, 1); // Simple Grinder
    }
    if (bot->HasSkill(SKILL_BLACKSMITHING) || bot->HasSkill(SKILL_ENGINEERING)) {
        StoreItem(5956, 1); // Blacksmith Hammer
    }
    if (bot->HasSkill(SKILL_ENGINEERING)) {
        StoreItem(6219, 1); // Arclight Spanner
    }
    if (bot->HasSkill(SKILL_ENCHANTING)) {
        StoreItem(44452, 1); // Runed Titanium Rod
    }
#ifdef LICH_KING
    if (bot->HasSkill(SKILL_INSCRIPTION)) {
        StoreItem(39505, 1); // Virtuoso Inking Set
    }
#endif
    if (bot->HasSkill(SKILL_SKINNING)) {
        StoreItem(7005, 1); // Skinning Knife
    }
}

Item* PlayerbotFactory::StoreItem(uint32 itemId, uint32 count)
{
//    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
    ItemPosCountVec sDest;
    InventoryResult msg = bot->CanStoreNewItem(INVENTORY_SLOT_BAG_0, NULL_SLOT, sDest, itemId, count);
    if (msg != EQUIP_ERR_OK)
        return NULL;

    return bot->StoreNewItem(sDest, itemId, true, Item::GenerateItemRandomPropertyId(itemId));
}

void PlayerbotFactory::InitInventoryTrade()
{
    vector<uint32> ids;
    ItemTemplateContainer const& itemTemplateContainer = sObjectMgr->GetItemTemplateStore();
    for (ItemTemplateContainer::const_iterator i = itemTemplateContainer.begin(); i != itemTemplateContainer.end(); ++i)
    {
//        ItemTemplate const& itemTemplate = i->second;
        uint32 itemId = i->first;
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        if (!proto)
            continue;

        if (proto->Class != ITEM_CLASS_TRADE_GOODS || proto->Bonding != NO_BIND)
            continue;

        if (proto->ItemLevel < bot->GetLevel())
            continue;

        if (proto->RequiredLevel > bot->GetLevel() || proto->RequiredLevel < bot->GetLevel() - 10)
            continue;

        if (proto->RequiredSkill && !bot->HasSkill(proto->RequiredSkill))
            continue;

        ids.push_back(itemId);
    }

    if (ids.empty())
    {
        sLog->outMessage("playerbot", LOG_LEVEL_ERROR, "No trade items available for bot %s (%d level)", bot->GetName().c_str(), bot->GetLevel());
        return;
    }

    uint32 index = urand(0, ids.size() - 1);
    if (index >= ids.size())
        return;

    uint32 itemId = ids[index];
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
    if (!proto)
        return;

    uint32 count = 1, stacks = 1;
    switch (proto->Quality)
    {
    case ITEM_QUALITY_NORMAL:
        count = proto->GetMaxStackSize();
        stacks = urand(1, 7) /* TODO PLAYERBOT / auctionbot.GetRarityPriceMultiplier(proto)*/;
        break;
    case ITEM_QUALITY_UNCOMMON:
        stacks = 1;
        count = urand(1, proto->GetMaxStackSize());
        break;
    case ITEM_QUALITY_RARE:
        stacks = 1;
        count = urand(1, min(uint32(3), proto->GetMaxStackSize()));
        break;
    }

    for (uint32 i = 0; i < stacks; i++)
        StoreItem(itemId, count);
}

void PlayerbotFactory::InitInventoryEquip()
{
    vector<uint32> ids;

    uint32 desiredQuality = itemQuality;
    if (urand(0, 100) < 100 * sPlayerbotAIConfig.randomGearLoweringChance && desiredQuality > ITEM_QUALITY_NORMAL) {
        desiredQuality--;
    }

    ItemTemplateContainer const& itemTemplateContainer = sObjectMgr->GetItemTemplateStore();
    for (ItemTemplateContainer::const_iterator i = itemTemplateContainer.begin(); i != itemTemplateContainer.end(); ++i)
    {
//        ItemTemplate const& itemTemplate = i->second;
        uint32 itemId = i->first;
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        if (!proto)
            continue;

        if ((proto->Class != ITEM_CLASS_ARMOR && proto->Class != ITEM_CLASS_WEAPON) || (proto->Bonding == BIND_WHEN_PICKED_UP ||
                proto->Bonding == BIND_WHEN_USE))
            continue;

        if (proto->Class == ITEM_CLASS_ARMOR && !CanEquipArmor(proto))
            continue;

        if (proto->Class == ITEM_CLASS_WEAPON && !CanEquipWeapon(proto))
            continue;

        if (!CanEquipItem(proto, desiredQuality))
            continue;

        ids.push_back(itemId);
    }

    int maxCount = urand(0, 3);
    int count = 0;
    for (int attempts = 0; attempts < 15; attempts++)
    {
        uint32 index = urand(0, ids.size() - 1);
        if (index >= ids.size())
            continue;

        uint32 itemId = ids[index];
        if (StoreItem(itemId, 1) && count++ >= maxCount)
            break;
   }
}

void PlayerbotFactory::InitGlyphs()
{
#ifdef LICH_KING
    bot->InitGlyphsForLevel();

    for (uint32 slotIndex = 0; slotIndex < MAX_GLYPH_SLOT_INDEX; ++slotIndex)
    {
        bot->SetGlyph(slotIndex, 0);
    }

    uint32 level = bot->GetLevel();
    uint32 maxSlot = 0;
    if (level >= 15)
        maxSlot = 2;
    if (level >= 30)
        maxSlot = 3;
    if (level >= 50)
        maxSlot = 4;
    if (level >= 70)
        maxSlot = 5;
    if (level >= 80)
        maxSlot = 6;

    if (!maxSlot)
        return;

    list<uint32> glyphs;
    ItemTemplateContainer const& itemTemplates = sObjectMgr->GetItemTemplateStore();
    for (ItemTemplateContainer::const_iterator i = itemTemplates.begin(); i != itemTemplates.end(); ++i)
    {
        uint32 itemId = i->first;
        ItemTemplate const* proto = &i->second;
        if (!proto)
            continue;

        if (proto->Class != ITEM_CLASS_GLYPH)
            continue;

        if ((proto->AllowableClass & bot->getClassMask()) == 0 || (proto->AllowableRace & bot->getRaceMask()) == 0)
            continue;

        for (uint32 spell = 0; spell < MAX_ITEM_PROTO_SPELLS; spell++)
        {
            uint32 spellId = proto->Spells[spell].SpellId;
            SpellInfo const *entry = sSpellMgr->GetSpellInfo(spellId);
            if (!entry)
                continue;

            for (uint32 effect = 0; effect <= EFFECT_2; ++effect)
            {
                if (entry->Effects[effect].Effect != SPELL_EFFECT_APPLY_GLYPH)
                    continue;

                uint32 glyph = entry->Effects[effect].MiscValue;
                glyphs.push_back(glyph);
            }
        }
    }

    if (glyphs.empty())
    {
        sLog->outMessage("playerbot", LOG_LEVEL_ERROR, "No glyphs found for bot %s", bot->GetName().c_str());
        return;
    }

    set<uint32> chosen;
    for (uint32 slotIndex = 0; slotIndex < maxSlot; ++slotIndex)
    {
        uint32 slot = bot->GetGlyphSlot(slotIndex);
        GlyphSlotEntry const *gs = sGlyphSlotStore.LookupEntry(slot);
        if (!gs)
            continue;

        vector<uint32> ids;
        for (list<uint32>::iterator i = glyphs.begin(); i != glyphs.end(); ++i)
        {
            uint32 id = *i;
            GlyphPropertiesEntry const *gp = sGlyphPropertiesStore.LookupEntry(id);
            if (!gp || gp->TypeFlags != gs->TypeFlags)
                continue;

            ids.push_back(id);
        }

        int maxCount = urand(0, 3);
        int count = 0;
        bool found = false;
        for (int attempts = 0; attempts < 15; ++attempts)
        {
            uint32 index = urand(0, ids.size() - 1);
            if (index >= ids.size())
                continue;

            uint32 id = ids[index];
            if (chosen.find(id) != chosen.end())
                continue;

            chosen.insert(id);

            bot->SetGlyph(slotIndex, id);
            found = true;
            break;
        }
        if (!found)
            sLog->outMessage("playerbot", LOG_LEVEL_ERROR, "No glyphs found for bot %s index %d slot %d", bot->GetName().c_str(), slotIndex, slot);
    }
#endif
}

void PlayerbotFactory::InitGuild()
{
    if (bot->GetGuildId())
        return;

    if (sPlayerbotAIConfig.randomBotGuilds.empty())
        RandomPlayerbotFactory::CreateRandomGuilds();

    vector<uint32> guilds;
    for(list<uint32>::iterator i = sPlayerbotAIConfig.randomBotGuilds.begin(); i != sPlayerbotAIConfig.randomBotGuilds.end(); ++i)
        guilds.push_back(*i);

    if (guilds.empty())
    {
        sLog->outMessage("playerbot", LOG_LEVEL_ERROR, "No random guilds available");
        return;
    }

    int index = urand(0, guilds.size() - 1);
    uint32 guildId = guilds[index];
    Guild* guild = sGuildMgr->GetGuildById(guildId);
    if (!guild)
    {
        sLog->outMessage("playerbot", LOG_LEVEL_ERROR, "Invalid guild %u", guildId);
        return;
    }

    if (guild->GetMemberCount() < 10)
    {
        SQLTransaction trans = CharacterDatabase.BeginTransaction();
        guild->AddMember(trans, bot->GetGUID(), urand(GR_OFFICER, GR_INITIATE));
        CharacterDatabase.CommitTransaction(trans);
    }
}