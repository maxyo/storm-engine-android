#pragma once
#include <unordered_map>

#include "../battle_command.h"

class BIShipCommandList : public BICommandList
{
  public:
    BIShipCommandList(entid_t eid, ATTRIBUTES *pA, VRenderer *rs);
    ~BIShipCommandList() override;

    void FillIcons() override;
#ifdef __ANDROID__
    int32_t AndroidTargetCount(const std::string &command) override;
#endif
  private:
#ifdef __ANDROID__
    std::unordered_map<std::string, std::pair<uint32_t, int32_t>> m_androidTargetCounts; // probe time, count
#endif
  protected:
    void Init() override;

  protected:
    void Release();

    int32_t ShipAdding(bool allLabel, bool bMyShip, bool bEnemy, bool bNeutral, bool bFriend);
    int32_t FortAdding(bool allLabel, bool bFriend, bool bNeutral, bool bEnemy);
    int32_t LandAdding(bool allLabel);
    int32_t CommandAdding();
    int32_t ChargeAdding();
    int32_t UserIconsAdding();
    int32_t AbilityAdding();
    int32_t AddCancelIcon();
    int32_t TownAdding(bool allLabel, bool bDiseased, bool bNotDiseased, bool bEnemy, bool bNeutral, bool bFriend);

    void AddFlagPictureToIcon(int32_t nCharIdx);

    int32_t m_nChargeTextureNum;
    int32_t m_nCommandTextureNum;

    std::vector<int32_t> m_aChargeQuantity;
};
