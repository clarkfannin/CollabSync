#include "SignalingClient.h"
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXNetSystem.h>

namespace
{
    // IXWebSocket needs WSAStartup on Windows; a no-op elsewhere. Guarded so it
    // runs exactly once for the process.
    void ensureNetSystem()
    {
        static const bool inited = [] { return ix::initNetSystem(); }();
        juce::ignoreUnused (inited);
    }
}

SignalingClient::SignalingClient() { ensureNetSystem(); }

SignalingClient::~SignalingClient() { close(); }

bool SignalingClient::connect (const juce::String& baseWssUrl,
                               const juce::String& roomCode,
                               Callbacks cbs)
{
    close();
    callbacks = std::move (cbs);

    const juce::String url = baseWssUrl
                           + (baseWssUrl.contains ("?") ? "&" : "?")
                           + "room=" + juce::URL::addEscapeChars (roomCode.toUpperCase(), true);

    ws = std::make_unique<ix::WebSocket>();
    ws->setUrl (url.toStdString());

    // Signaling is short-lived (seconds until the P2P link is up), so we do not
    // want IXWebSocket's default auto-reconnect fighting an intentional close.
    ws->disableAutomaticReconnection();

    ws->setOnMessageCallback ([this] (const ix::WebSocketMessagePtr& msg)
    {
        switch (msg->type)
        {
            case ix::WebSocketMessageType::Open:
                open.store (true);
                break;

            case ix::WebSocketMessageType::Message:
                handleMessage (msg->str);
                break;

            case ix::WebSocketMessageType::Close:
                open.store (false);
                if (callbacks.onClosed) callbacks.onClosed();
                break;

            case ix::WebSocketMessageType::Error:
                open.store (false);
                if (callbacks.onError)
                    callbacks.onError (msg->errorInfo.reason);
                break;

            default:
                break; // Ping/Pong/Fragment — ignore
        }
    });

    ws->start();
    return true;
}

void SignalingClient::close()
{
    if (ws)
    {
        ws->stop();
        ws.reset();
    }
    open.store (false);
}

void SignalingClient::handleMessage (const std::string& text)
{
    juce::var msg = juce::JSON::parse (juce::String (text));
    if (! msg.isObject())
        return;

    const juce::String type = msg.getProperty ("type", "").toString();

    if (type == "role")
    {
        const bool isHost = msg.getProperty ("role", "guest").toString() == "host";
        if (callbacks.onRole) callbacks.onRole (isHost);
    }
    else if (type == "waiting")
    {
        if (callbacks.onWaiting) callbacks.onWaiting();
    }
    else if (type == "peer-joined")
    {
        if (callbacks.onPeerJoined) callbacks.onPeerJoined();
    }
    else if (type == "peer-left")
    {
        if (callbacks.onPeerLeft) callbacks.onPeerLeft();
    }
    else if (type == "description")
    {
        if (callbacks.onRemoteDescription)
            callbacks.onRemoteDescription (msg.getProperty ("sdp", "").toString().toStdString());
    }
    else if (type == "candidate")
    {
        if (callbacks.onRemoteCandidate)
            callbacks.onRemoteCandidate (msg.getProperty ("candidate", "").toString().toStdString());
    }
    else if (type == "pubkey")
    {
        const juce::String b64 = msg.getProperty ("key", "").toString();
        juce::MemoryOutputStream decoded;
        if (juce::Base64::convertFromBase64 (decoded, b64) && callbacks.onRemotePubkey)
        {
            const auto* p = static_cast<const uint8_t*> (decoded.getData());
            callbacks.onRemotePubkey (std::vector<uint8_t> (p, p + decoded.getDataSize()));
        }
    }
    else if (type == "error")
    {
        if (callbacks.onError)
            callbacks.onError (msg.getProperty ("reason", "unknown").toString().toStdString());
    }
}

void SignalingClient::sendJson (const juce::var& obj)
{
    if (ws && open.load())
        ws->send (juce::JSON::toString (obj, true).toStdString());
}

void SignalingClient::sendDescription (const std::string& sdp)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("type", "description");
    o->setProperty ("sdp",  juce::String (sdp));
    sendJson (juce::var (o));
}

void SignalingClient::sendCandidate (const std::string& candidate)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("type",      "candidate");
    o->setProperty ("candidate", juce::String (candidate));
    sendJson (juce::var (o));
}

void SignalingClient::sendPubkey (const std::vector<uint8_t>& publicKey)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("type", "pubkey");
    o->setProperty ("key",  juce::Base64::toBase64 (publicKey.data(), publicKey.size()));
    sendJson (juce::var (o));
}
