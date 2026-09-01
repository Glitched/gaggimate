#pragma once
#ifndef PROFILEMANAGER_H
#define PROFILEMANAGER_H
#include "PluginManager.h"
#include <FS.h>
#include <display/core/Settings.h>
#include <display/core/utils.h>
#include <display/models/profile.h>

// Generated ids are short alphanumeric strings; seed profiles use names like
// "9bar". 64 is well clear of both and bounds the filename.
constexpr unsigned int MAX_PROFILE_ID_LENGTH = 64;

class ProfileManager {
  public:
    ProfileManager(fs::FS *fs, String dir, Settings &settings, PluginManager *plugin_manager);

    void setup();
    // Bumped on every mutation (save, delete, select, favourite, reorder). Response caches
    // key on it, so a stale list can never be served and nothing needs a cache API.
    uint32_t getRevision() const { return revision; }
    void bumpRevision() { revision++; }
    std::vector<String> listProfiles();
    bool loadProfile(const String &uuid, Profile &outProfile);
    bool saveProfile(Profile &profile);
    bool deleteProfile(const String &uuid);
    bool profileExists(const String &uuid);
    void selectProfile(const String &uuid);
    Profile &getSelectedProfile();
    bool loadSelectedProfile(Profile &outProfile);
    std::vector<String> getFavoritedProfiles(bool validate = false);

    void addFavoritedProfile(String id);
    void removeFavoritedProfile(String id);

  private:
    Profile selectedProfile{};
    PluginManager *_plugin_manager;
    Settings &_settings;
    fs::FS *_fs;
    String _dir;
    uint32_t revision = 1;
    bool ensureDirectory() const;
    String profilePath(const String &uuid) const;

    // Rejects ids that would escape _dir when turned into a filename.
    static bool isValidProfileId(const String &uuid);
    void migrate(const std::vector<String> &existingProfiles);
};

#endif // PROFILEMANAGER_H
