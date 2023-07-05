# IsomTerrain
A standalone, open-source implementation of StarCraft's isometric terrain editing as found in the staredit campaign editor bundled with early versions of StarCraft.

- [IsomApi.h](https://github.com/TheNitesWhoSay/IsomTerrain/blob/main/IsomTerrain/IsomApi.h) - centralized collection of ISOM-related code
- [IsomTests.cpp](https://github.com/TheNitesWhoSay/IsomTerrain/blob/main/IsomTerrain/IsomTests.cpp) - automation tests & example uses

The above files contain what should be of interest in relation to ISOM new-map generation, editing, and resizing. There is a lot of supporting code here from [Chkdraft](https://github.com/TheNitesWhoSay/Chkdraft) - MappingCoreLib and its dependencies: CascLib, CrossCutLib, IcuLib, rarecpp, StormLib. The are primarily for loading & parsing maps but it shouldn't be important for understanding ISOM itself. See Chkdraft if you're interested in a GUI application making use of code (though unlike in this project it will be factored out to several files where appropriate).

# Credits
- Blizzard Entertainment - creator and owner of [StarCraft](https://starcraft.com/en-us/) and the staredit campaign editor.
- SI - author of Scmdraft found at [stormcoast-fortress](http://www.stormcoast-fortress.net/) for providing the ISOM code (originally decompiled from staredit with several revisions) which was heavily drawn upon to create this implementation; as well as some help along the way.
- lawine - author of [another code repository](https://github.com/TheNitesWhoSay/lawine) drawn on in creating this implementation.
- saintofidiocy - author of [isom-data-repair-tool](https://github.com/saintofidiocy/ISOM) for yet more ISOM code as well as terrain file format documentation, terrain rendering code, and additional research data and help.
- TheNitesWhoSay (me) - for this independent code implementation which organizes everything into a standalone, easily runnable, open-source project; as well as creating the support code from [Chkdraft](https://github.com/TheNitesWhoSay/Chkdraft) and putting together the automation tests.
