#pragma once

#include <functional>
#include <memory>

#include <juce_core/juce_core.h>

namespace blockrig
{

/// Client for the official TONE3000 API (tone3000.com/api).
///
/// TONE3000 explicitly invites product integrations: OAuth with PKCE, localhost
/// redirect URIs allowed, and documented search/model/download endpoints. Two
/// credentials are involved: a *publishable key* identifying this app (free,
/// created at tone3000.com/api) and a per-user token from signing in, which is
/// what makes model downloads valid. Both persist in Tone3000.settings.
///
/// Every network call runs on a background thread; callbacks land on the
/// message thread.
class Tone3000Client final
{
public:
    explicit Tone3000Client(const juce::File& settingsFile);
    ~Tone3000Client();

    /// The app key from tone3000.com/api ("t3k_pub_…").
    void setPublishableKey(const juce::String& key);
    juce::String getPublishableKey() const;

    bool isSignedIn() const;
    juce::String getSignedInUser() const;

    /// Opens the system browser on the TONE3000 authorize page and listens on a
    /// localhost port for the redirect. PKCE, so no client secret exists to leak.
    void signIn(std::function<void(bool success, juce::String error)> onFinished);
    void signOut();

    struct Tone
    {
        juce::String id;
        juce::String title;
        juce::String author;
        juce::String gear;
        /// Direct model download URL when the search response carries one;
        /// otherwise resolved via the models endpoint at download time.
        juce::String modelUrl;
    };

    void search(const juce::String& query,
                std::function<void(juce::Array<Tone>, juce::String error)> onFinished);

    /// Downloads a tone's model file and hands back the temp file; the caller
    /// moves it into the capture library.
    void downloadModel(const Tone& tone,
                       std::function<void(juce::File file, juce::String error)> onFinished);

private:
    class Impl;
    std::unique_ptr<Impl> mImpl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Tone3000Client)
};

} // namespace blockrig
