// Ids that mean different messages in TEHO and Caribbean Legend resolve per game profile
// (src/libs/core/include/colliding_message_ids.hpp); every other id is not colliding.
#include "colliding_message_ids.hpp"
#include "shared/messages.h"
#include "shared/sound.h"

#include <cstdio>
#include <cstdlib>

using storm::CollidingIdDomain;
using storm::CollidingIdMeaning;
using storm::GameProfile;

namespace
{
int failures = 0;

void expect(bool ok, const char *what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

CollidingIdMeaning meaning(GameProfile p, CollidingIdDomain d, int v)
{
    return storm::collidingIdMeaning(p, d, v);
}
} // namespace

int main()
{
    // 54000
    expect(MSG_WHR_LIGHTNING_ADD == 54000 && MSG_WHR_UPDATE_STATE == 54000, "54000 is both ids");
    expect(meaning(GameProfile::TEHO, CollidingIdDomain::Message, 54000) == CollidingIdMeaning::WhrLightningAdd,
           "TEHO 54000 = MSG_WHR_LIGHTNING_ADD");
    expect(meaning(GameProfile::CaribbeanLegend, CollidingIdDomain::Message, 54000) == CollidingIdMeaning::WhrUpdateState,
           "CL 54000 = MSG_WHR_UPDATE_STATE");
    // 77012
    expect(MSG_SOUND_RESUME == 77012 && MSG_SOUND_PAUSE == 77012, "77012 is both ids");
    expect(meaning(GameProfile::TEHO, CollidingIdDomain::Message, 77012) == CollidingIdMeaning::SoundResume,
           "TEHO 77012 = MSG_SOUND_RESUME");
    expect(meaning(GameProfile::CaribbeanLegend, CollidingIdDomain::Message, 77012) == CollidingIdMeaning::SoundPause,
           "CL 77012 = MSG_SOUND_PAUSE");
    // SoundPlay type 3
    expect(SOUND_MP3_3D == 3 && SOUND_FMOD_EVENT == 3, "3 is both sound types");
    expect(meaning(GameProfile::TEHO, CollidingIdDomain::SoundPlayType, 3) == CollidingIdMeaning::SoundTypeMp3_3D,
           "TEHO type 3 = SOUND_MP3_3D");
    expect(meaning(GameProfile::CaribbeanLegend, CollidingIdDomain::SoundPlayType, 3) ==
               CollidingIdMeaning::SoundTypeFmodEvent,
           "CL type 3 = SOUND_FMOD_EVENT");
    // the domain matters, other ids are not colliding
    expect(meaning(GameProfile::CaribbeanLegend, CollidingIdDomain::Message, 3) == CollidingIdMeaning::None,
           "message 3 is not colliding");
    expect(meaning(GameProfile::TEHO, CollidingIdDomain::Message, MSG_SOUND_PLAY) == CollidingIdMeaning::None,
           "MSG_SOUND_PLAY is not colliding");
    expect(storm::collidingIdName(CollidingIdMeaning::SoundPause) == "MSG_SOUND_PAUSE", "names");
    if (failures)
        return EXIT_FAILURE;
    std::puts("colliding message ids: OK");
    return EXIT_SUCCESS;
}
