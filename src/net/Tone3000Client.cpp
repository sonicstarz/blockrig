#include "net/Tone3000Client.h"

#include <juce_cryptography/juce_cryptography.h>
#include <juce_events/juce_events.h>

namespace blockrig
{
namespace
{
constexpr const char* kApiBase = "https://www.tone3000.com/api/v1";

juce::String base64Url(const juce::MemoryBlock& data)
{
    return juce::Base64::toBase64(data.getData(), data.getSize())
        .replaceCharacter('+', '-')
        .replaceCharacter('/', '_')
        .removeCharacters("=");
}
} // namespace

class Tone3000Client::Impl : private juce::Thread
{
public:
    explicit Impl(const juce::File& settingsFile)
        : juce::Thread("TONE3000")
        , mSettingsFile(settingsFile)
    {
        load();
    }

    ~Impl() override
    {
        stopThread(4000);
    }

    //==========================================================================
    juce::File mSettingsFile;
    juce::String mPublishableKey, mAccessToken, mRefreshToken, mUserName;
    juce::CriticalSection mLock;

    void load()
    {
        if (const auto parsed = juce::JSON::parse(mSettingsFile.loadFileAsString()); parsed.isObject())
        {
            mPublishableKey = parsed.getProperty("publishableKey", "").toString();
            mAccessToken = parsed.getProperty("accessToken", "").toString();
            mRefreshToken = parsed.getProperty("refreshToken", "").toString();
            mUserName = parsed.getProperty("userName", "").toString();
        }
    }

    void save()
    {
        auto* object = new juce::DynamicObject();
        object->setProperty("publishableKey", mPublishableKey);
        object->setProperty("accessToken", mAccessToken);
        object->setProperty("refreshToken", mRefreshToken);
        object->setProperty("userName", mUserName);

        mSettingsFile.getParentDirectory().createDirectory();
        mSettingsFile.replaceWithText(juce::JSON::toString(juce::var(object)));
    }

    //==========================================================================
    // All requests carry the user token when signed in, else the app key. The
    // API answers 401 to both when they are wrong, which the UI surfaces as-is.
    juce::String bearerToken() const
    {
        const juce::ScopedLock lock(mLock);
        return mAccessToken.isNotEmpty() ? mAccessToken : mPublishableKey;
    }

    juce::String get(const juce::String& endpoint, int& statusOut)
    {
        const juce::URL url(juce::String(kApiBase) + endpoint);

        auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                           .withExtraHeaders("Authorization: Bearer " + bearerToken() + "\r\n")
                           .withConnectionTimeoutMs(10000)
                           .withStatusCode(&statusOut);

        if (auto stream = url.createInputStream(options))
            return stream->readEntireStreamAsString();

        statusOut = 0;
        return {};
    }

    juce::String post(const juce::String& endpoint, const juce::String& formBody, int& statusOut)
    {
        const auto url = juce::URL(juce::String(kApiBase) + endpoint).withPOSTData(formBody);

        auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                           .withExtraHeaders("Content-Type: application/x-www-form-urlencoded\r\n")
                           .withConnectionTimeoutMs(10000)
                           .withStatusCode(&statusOut);

        if (auto stream = url.createInputStream(options))
            return stream->readEntireStreamAsString();

        statusOut = 0;
        return {};
    }

    //==========================================================================
    // Work queue: one job at a time is plenty for a browse-and-download panel.
    struct Job
    {
        std::function<void()> body;
    };

    std::vector<Job> mJobs;

    bool shouldExit() { return threadShouldExit(); }

    void enqueue(std::function<void()> body)
    {
        {
            const juce::ScopedLock lock(mLock);
            mJobs.push_back({std::move(body)});
        }
        startThread();
        notify();
    }

    void run() override
    {
        while (!threadShouldExit())
        {
            Job job;

            {
                const juce::ScopedLock lock(mLock);
                if (!mJobs.empty())
                {
                    job = std::move(mJobs.front());
                    mJobs.erase(mJobs.begin());
                }
            }

            if (job.body)
                job.body();
            else if (!wait(500))
                continue;

            {
                const juce::ScopedLock lock(mLock);
                if (mJobs.empty())
                    return;
            }
        }
    }

    //==========================================================================
    void refreshAccessToken()
    {
        juce::String refresh;
        {
            const juce::ScopedLock lock(mLock);
            refresh = mRefreshToken;
        }

        if (refresh.isEmpty())
            return;

        int status = 0;
        const auto body = post("/oauth/token",
                               "grant_type=refresh_token&refresh_token="
                                   + juce::URL::addEscapeChars(refresh, true),
                               status);

        if (const auto parsed = juce::JSON::parse(body); status == 200 && parsed.isObject())
        {
            const juce::ScopedLock lock(mLock);
            mAccessToken = parsed.getProperty("access_token", mAccessToken).toString();
            mRefreshToken = parsed.getProperty("refresh_token", mRefreshToken).toString();
            juce::MessageManager::callAsync([this] { save(); });
        }
    }

    /// GET with one automatic refresh-and-retry on 401.
    juce::String getAuthed(const juce::String& endpoint, int& statusOut)
    {
        auto body = get(endpoint, statusOut);

        if (statusOut == 401 && mRefreshToken.isNotEmpty())
        {
            refreshAccessToken();
            body = get(endpoint, statusOut);
        }

        return body;
    }
};

//==============================================================================
Tone3000Client::Tone3000Client(const juce::File& settingsFile)
    : mImpl(std::make_unique<Impl>(settingsFile))
{
}

Tone3000Client::~Tone3000Client() = default;

void Tone3000Client::setPublishableKey(const juce::String& key)
{
    {
        const juce::ScopedLock lock(mImpl->mLock);
        mImpl->mPublishableKey = key.trim();
    }
    mImpl->save();
}

juce::String Tone3000Client::getPublishableKey() const
{
    const juce::ScopedLock lock(mImpl->mLock);
    return mImpl->mPublishableKey;
}

bool Tone3000Client::isSignedIn() const
{
    const juce::ScopedLock lock(mImpl->mLock);
    return mImpl->mAccessToken.isNotEmpty();
}

juce::String Tone3000Client::getSignedInUser() const
{
    const juce::ScopedLock lock(mImpl->mLock);
    return mImpl->mUserName;
}

void Tone3000Client::signOut()
{
    {
        const juce::ScopedLock lock(mImpl->mLock);
        mImpl->mAccessToken.clear();
        mImpl->mRefreshToken.clear();
        mImpl->mUserName.clear();
    }
    mImpl->save();
}

void Tone3000Client::signIn(std::function<void(bool, juce::String)> onFinished)
{
    mImpl->enqueue([this, onFinished] {
        const auto fail = [onFinished](juce::String why) {
            juce::MessageManager::callAsync([onFinished, why] { onFinished(false, why); });
        };

        // PKCE verifier and its S256 challenge.
        juce::Random random;
        juce::MemoryBlock verifierBytes(32);
        for (size_t i = 0; i < verifierBytes.getSize(); ++i)
            verifierBytes[i] = static_cast<char>(random.nextInt(256));

        const auto verifier = base64Url(verifierBytes);
        const juce::SHA256 challengeHash(verifier.toRawUTF8(), verifier.getNumBytesAsUTF8());
        const auto challenge = base64Url(challengeHash.getRawData());

        // Loopback listener for the redirect. Port 0 is not portable across
        // JUCE socket backends, so probe a small fixed range instead.
        juce::StreamingSocket listener;
        int port = 0;

        for (int candidate = 53820; candidate < 53830; ++candidate)
        {
            if (listener.createListener(candidate, "127.0.0.1"))
            {
                port = candidate;
                break;
            }
        }

        if (port == 0)
        {
            fail("Could not open a local port for the sign-in redirect.");
            return;
        }

        const auto redirect = "http://127.0.0.1:" + juce::String(port) + "/cb";

        juce::URL authorize(juce::String(kApiBase) + "/oauth/authorize");
        authorize = authorize.withParameter("response_type", "code")
                        .withParameter("client_id", mImpl->mPublishableKey)
                        .withParameter("redirect_uri", redirect)
                        .withParameter("code_challenge", challenge)
                        .withParameter("code_challenge_method", "S256");

        juce::MessageManager::callAsync([authorize] { authorize.launchInDefaultBrowser(); });

        // Wait for the browser to come back. 3 minutes is enough to type a
        // password; an abandoned sign-in must not pin this thread forever, and
        // waitForNextConnection alone would block indefinitely.
        bool ready = false;

        for (int elapsedMs = 0; elapsedMs < 180000 && !mImpl->shouldExit(); elapsedMs += 500)
        {
            if (listener.waitUntilReady(true, 500) == 1)
            {
                ready = true;
                break;
            }
        }

        std::unique_ptr<juce::StreamingSocket> connection(ready ? listener.waitForNextConnection()
                                                                : nullptr);

        if (connection == nullptr)
        {
            fail("Sign-in timed out.");
            return;
        }

        char buffer[4096] = {};
        connection->waitUntilReady(true, 5000);
        connection->read(buffer, sizeof(buffer) - 1, false);

        const juce::String request(buffer);
        const auto code = request.fromFirstOccurrenceOf("code=", false, false)
                              .upToFirstOccurrenceOf("&", false, false)
                              .upToFirstOccurrenceOf(" ", false, false)
                              .trim();

        const juce::String reply = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                                   "<html><body style='background:#101216;color:#e8eaed;"
                                   "font-family:sans-serif;text-align:center;padding-top:120px'>"
                                   "<h2>Connected to BlockRig</h2>You can close this tab and return "
                                   "to the app.</body></html>";
        connection->write(reply.toRawUTF8(), static_cast<int>(reply.getNumBytesAsUTF8()));
        connection->close();

        if (code.isEmpty())
        {
            fail("The sign-in redirect carried no code.");
            return;
        }

        int status = 0;
        const auto body = mImpl->post("/oauth/token",
                                      "grant_type=authorization_code&code="
                                          + juce::URL::addEscapeChars(code, true)
                                          + "&redirect_uri=" + juce::URL::addEscapeChars(redirect, true)
                                          + "&client_id="
                                          + juce::URL::addEscapeChars(mImpl->mPublishableKey, true)
                                          + "&code_verifier=" + juce::URL::addEscapeChars(verifier, true),
                                      status);

        const auto parsed = juce::JSON::parse(body);

        if (status != 200 || !parsed.isObject())
        {
            fail("Token exchange failed (" + juce::String(status) + "): " + body.substring(0, 200));
            return;
        }

        {
            const juce::ScopedLock lock(mImpl->mLock);
            mImpl->mAccessToken = parsed.getProperty("access_token", "").toString();
            mImpl->mRefreshToken = parsed.getProperty("refresh_token", "").toString();
        }

        // Best effort: put a name on the session.
        int userStatus = 0;
        if (const auto user = juce::JSON::parse(mImpl->getAuthed("/user", userStatus)); user.isObject())
        {
            const juce::ScopedLock lock(mImpl->mLock);
            mImpl->mUserName = user.getProperty("username", user.getProperty("name", "")).toString();
        }

        juce::MessageManager::callAsync([this, onFinished] {
            mImpl->save();
            onFinished(true, {});
        });
    });
}

void Tone3000Client::search(const juce::String& query,
                            std::function<void(juce::Array<Tone>, juce::String)> onFinished)
{
    mImpl->enqueue([this, query, onFinished] {
        int status = 0;
        const auto body = mImpl->getAuthed("/tones/search?query=" + juce::URL::addEscapeChars(query, true),
                                           status);

        juce::Array<Tone> tones;
        juce::String error;

        const auto parsed = juce::JSON::parse(body);

        if (status != 200)
        {
            error = status == 401
                        ? juce::String("Not authorized - check the API key, or sign in.")
                        : "Search failed (" + juce::String(status) + ")";
        }
        else
        {
            // The list may arrive bare or wrapped; take whichever is an array.
            auto list = parsed;
            if (!list.isArray())
                for (const auto* key : {"tones", "data", "results", "items"})
                    if (parsed.getProperty(key, {}).isArray())
                    {
                        list = parsed.getProperty(key, {});
                        break;
                    }

            if (const auto* array = list.getArray())
            {
                for (const auto& item : *array)
                {
                    Tone tone;
                    tone.id = item.getProperty("id", "").toString();
                    tone.title = item.getProperty("title", item.getProperty("name", "")).toString();
                    tone.author = item.getProperty("username",
                                                   item.getProperty("author", "")).toString();
                    tone.gear = item.getProperty("gear", item.getProperty("gear_type", "")).toString();
                    tone.modelUrl = item.getProperty("model_url", "").toString();

                    if (tone.id.isNotEmpty() || tone.modelUrl.isNotEmpty())
                        tones.add(tone);
                }
            }
            else
            {
                error = "Unexpected search response: " + body.substring(0, 160);
            }
        }

        juce::MessageManager::callAsync([onFinished, tones, error] { onFinished(tones, error); });
    });
}

void Tone3000Client::downloadModel(const Tone& tone,
                                   std::function<void(juce::File, juce::String)> onFinished)
{
    mImpl->enqueue([this, tone, onFinished] {
        const auto fail = [onFinished](juce::String why) {
            juce::MessageManager::callAsync([onFinished, why] { onFinished({}, why); });
        };

        auto modelUrl = tone.modelUrl;

        // No direct URL in the search row: ask the models endpoint.
        if (modelUrl.isEmpty())
        {
            int status = 0;
            const auto body = mImpl->getAuthed("/models?tone_id="
                                                   + juce::URL::addEscapeChars(tone.id, true),
                                               status);
            const auto parsed = juce::JSON::parse(body);

            auto list = parsed;
            if (!list.isArray())
                for (const auto* key : {"models", "data", "items"})
                    if (parsed.getProperty(key, {}).isArray())
                    {
                        list = parsed.getProperty(key, {});
                        break;
                    }

            if (const auto* array = list.getArray(); array != nullptr && !array->isEmpty())
                modelUrl = array->getFirst()
                               .getProperty("model_url",
                                            array->getFirst().getProperty("url", ""))
                               .toString();

            if (modelUrl.isEmpty())
            {
                fail("No downloadable model on this tone (" + juce::String(status)
                     + "). Sign-in may be required.");
                return;
            }
        }

        int status = 0;
        const juce::URL url(modelUrl);
        auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                           .withExtraHeaders("Authorization: Bearer " + mImpl->bearerToken() + "\r\n")
                           .withConnectionTimeoutMs(20000)
                           .withStatusCode(&status);

        auto stream = url.createInputStream(options);

        if (stream == nullptr || (status != 200 && status != 0))
        {
            fail("Download failed (" + juce::String(status) + ")");
            return;
        }

        auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile(juce::File::createLegalFileName(
                                          tone.title.isNotEmpty() ? tone.title : "TONE3000 capture")
                                      + ".nam")
                        .getNonexistentSibling();

        juce::FileOutputStream out(temp);

        if (!out.openedOk() || out.writeFromInputStream(*stream, -1) <= 0)
        {
            fail("Could not write the downloaded file.");
            return;
        }

        out.flush();
        juce::MessageManager::callAsync([onFinished, temp] { onFinished(temp, {}); });
    });
}

} // namespace blockrig
