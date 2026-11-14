R"STORMDATA(# The parallax lists, shipped. One `include <texture>` or `exclude <texture>` a line; the texture as the engine names it
# (lower case, '/' folders, no extension). By default a texture takes the parallax when its class does (masonry: stone
# walls, cobbles, brick, the house atlases) and enough of it is trusted (parallax_trust.hpp). Here: what takes it
# although it is not masonry (plank floors, decks, piers), and what does not although it is (signs). The owner's choices
# from the in-game debug view (pom_user_exclusions.txt in the app's files directory) are folded in here.
exclude signboardsa
exclude signboardsf
exclude signboardsh
exclude signboardss
exclude signboardshipsu1
exclude signboard_gwc
exclude quest_signs
exclude hol_signs
include pierwood1
include deckplanksu1
include deckplanksu2
include deckplankst1
include deckplankst2
include deckplankst4
include deckplankst5
include deckplankst6
include deck
include deck2
include deck_new02
include deck_new03
include deck_new04
)STORMDATA"
