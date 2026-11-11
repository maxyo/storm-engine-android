#pragma once

#include "xi_image.h"
#include "xi_picture.h"

class CXI_SCROLLEDPICTURE : public CXI_PICTURE
{
  public:
    CXI_SCROLLEDPICTURE();
    ~CXI_SCROLLEDPICTURE() override;
    void Draw(bool bSelected, uint32_t Delta_Time) override;
    bool Init(INIFILE *ini1, const char *name1, INIFILE *ini2, const char *name2, VRenderer *rs, XYRECT &hostRect,
              XYPOINT &ScreenSize) override;
    void ReleaseAll() override;
    int CommandExecute(int wActCode) override;
    bool IsClick(int buttonID, int32_t xPos, int32_t yPos) override;

    void MouseThis(float fX, float fY) override
    {
    }

    void ChangePosition(XYRECT &rNewPos) override;
    void SaveParametersToIni() override;
    uint32_t MessageProc(int32_t msgcode, MESSAGE &message) override;
    void MoveMouseOutScreen(float fX, float fY) override;
    void ChangeUV(FXYRECT &frNewUV) override;
#ifdef __ANDROID__
    void MobileCollect(std::vector<MobileWidget> &widgets) const override;
    bool MobileApply(int index, const std::string &value) override;
    int RClickCommand() const;
#endif

  protected:
    void LoadIni(INIFILE *ini1, const char *name1, INIFILE *ini2, const char *name2) override;
    void SetNewPicture(bool video, char *sNewTexName);
    void SetNewPictureFromDir(char *dirName);
    void RecalculateTexPerPixel();
    void UpdateBuildenImages();
    void SetPosToCenter(float fX, float fY);
    void SetScale(int32_t nScaleIdx);
    void SetScale(float fsx, float fsy);

    float m_fUTexPerPixel;
    float m_fVTexPerPixel;

    FXYPOINT m_fpBaseSize;

    struct BuildinImage
    {
        CXI_IMAGE *pImg;
        FXYPOINT fpPos;
        FXYPOINT fpSize;
        bool bShow;
        std::string id;          // name in the interface's imagelist ("FortFrance", "PShip")
        std::string group, pic;  // picture of pictures.ini, or
        std::string file;        // a texture file
    };

    int32_t m_nScaleNum;
    std::vector<FXYPOINT> m_aScale;
    std::vector<BuildinImage> m_aImg;
};
