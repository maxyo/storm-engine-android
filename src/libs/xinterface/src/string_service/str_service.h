#pragma once

#include "vma.hpp"

#include "script_libriary.h"
#include "../string_service.h"
#include "user_string_files.hpp"

//-----------SDEVICE-----------
class STRSERVICE : public VSTRSERVICE
{
  public:
    STRSERVICE();
    ~STRSERVICE() override;

    bool Init() override;
    bool LoadState(ENTITY_STATE *state) override;
    bool CreateState(ENTITY_STATE_GEN *state_gen) override;
    void RunStart() override;
    void RunEnd() override;

    void SetLanguage(const char *sLanguage) override;
    char *GetLanguage() override;

    char *GetString(const char *stringName, char *sBuffer = nullptr, std::size_t bufferSize = 0) override;
    int32_t GetStringNum(const char *stringName) override;
    char *GetString(int32_t strNum) override;
    char *GetStringName(int32_t strNum) override;

    int32_t OpenUsersStringFile(const char *fileName) override;
    void CloseUsersStringFile(int32_t id) override;
    char *TranslateFromUsers(int32_t id, const char *inStr) override;

    void SetDialogSourceFile(const char *fileName);

    char *TranslateForDialog(const char *str)
    {
        return TranslateFromUsers(m_nDialogSourceFile, (char *)str);
    }

  protected:
    void LoadIni();
    std::optional<storm::strings::UserStringFiles::Rows> LoadUsersStringFile(const std::string &fileName) const;
    bool GetNextUsersString(char *src, int32_t &idx, std::optional<std::string> *strName,
                            std::optional<std::string> *strData) const;

  protected:
    char *m_sLanguage;
    char *m_sIniFileName;
    char *m_sLanguageDir;

    int32_t m_nStringQuantity;
    char **m_psStrName;
    char **m_psString;

    // Kept parsed after their last close: the interface scripts reopen the same few files
    // every frame (user_string_files.hpp).
    storm::strings::UserStringFiles m_usersFiles{32};

    int32_t m_nDialogSourceFile;
};

class SCRIPT_INTERFACE_FUNCTIONS : public SCRIPT_LIBRIARY
{
  public:
    SCRIPT_INTERFACE_FUNCTIONS(){};

    ~SCRIPT_INTERFACE_FUNCTIONS() override{};
    bool Init() override;
};
