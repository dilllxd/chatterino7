# itzon.tv support

This fork adds native, multi-account itzon.tv chat to Chatterino7. It supports
the documented OAuth 2.0 public-client flow with PKCE and retains dedicated
chat bot tokens as a fallback.

## Use it

1. Open **Settings > Accounts > Add > itzon.tv > Basic**, choose
   **Log in (Opens in browser)**, and approve the `identity`, `chat`, and
   `api:read` scopes.
2. For the fallback flow, create a chat bot token in the
   [itzon.tv integration dashboard](https://itzon.tv/dashboard/integration),
   then open **Advanced**, enter the account name and token, and choose
   **Add user**. Re-adding the same name updates a rotated token.
3. Change a split's channel, choose **itzon.tv**, and enter the channel's
   account name.
4. Use the account switcher to choose which saved itzon.tv account joins the
   open channels and sends the next messages.

## OAuth public-client setup

itzon.tv staff must register the desktop app and issue its 32-hex public
`client_id`. Register it as a **public client**, never a confidential client,
with this loopback redirect URI:

`http://127.0.0.1:38276/oauth/itzon/callback`

The port in that registered URI is a placeholder. At login Chatterino binds a
free ephemeral `127.0.0.1` port and uses that exact port in both the authorize
request and token exchange. itzon.tv's documented RFC 8252 loopback exception
allows the runtime port to differ while the host and path remain exact.

This fork embeds its issued public client ID because it is an
identifier and is safe to ship. Developers can set
`CHATTERINO_ITZON_OAUTH_CLIENT_ID` in the environment while configuring CMake
to override it for another registered public client. Never embed or request a
client secret. An empty or malformed override leaves the OAuth button disabled
without affecting chat bot token accounts.

The login flow generates a fresh 43-character verifier, sends an S256 PKCE
challenge and random state, exchanges the single-use code immediately, then
fetches `/api/oauth/userinfo`. Access and rotating refresh tokens are encrypted
with DPAPI on Windows. Access tokens are refreshed before their one-hour
expiry, the new refresh token replaces the consumed one, and IRC reconnects
immediately because the successful refresh revokes the previous access token.

The split menu and its `Open channel in browser` hotkey open the matching
`https://itzon.tv/{channel}` page. itzon splits also expose the provider-neutral
Twitch conveniences that apply to them: reload channel emotes, mute highlight
sounds, live notifications, replies, and moderation mode.

## Chat commands

itzon.tv implements its chat commands in the IRC server. In an itzon split,
Chatterino passes the following commands through to itzon instead of invoking
the same-named Twitch or Kick API command:

- Everyone: `/whisper <user> <message>` and `/w <user> <message>`.
- Moderators and above: `/ban <user>`, `/timeout <user> <minutes>`,
  `/unban <user>`, `/pin <message-id>`, `/unpin`, and
  `/delete <message-id>`.
- Channel owner: `/mod <user>`, `/unmod <user>`, `/vip <user>`, and
  `/unvip <user>`.

The commands also work with a `.` prefix, matching the itzon.tv web client.
Replies use the message hover action and the IRC `+reply` tag; received replies
are grouped into Chatterino reply threads.

## Rich live notifications

The selected OAuth account supplies its access token for Public API reads when
it has `api:read`. The build can also embed a separate itzon.tv **Public API**
personal token with only that scope as a fallback. Set the
`CHATTERINO_ITZON_API_TOKEN` environment variable only while configuring the
build with CMake; it is placed in a generated build header and then compiled
into the executable. Do not commit the token or use the chat bot token from
**Settings > Accounts** here.

With that credential, live notifications use the documented
`/api/public/v1/channel/{username}` endpoint and include the stream title. If
the embedded token is absent, invalid, lacks `api:read`, or a request fails,
the client falls back to the documented no-token badge endpoint. OAuth and
personal-token lookup are isolated behind `ItzonApiToken` rather than being
mixed into IRC chat authentication.

Open itzon.tv splits also combine that authenticated metadata with the public
live-channel response. Their headers and hover cards show live state, title,
category, viewers, followers, language, uptime, and the current stream preview.
Saved live tabs are marked live as soon as the startup layout is restored.

Saved accounts stay authenticated for fast switching, but only the selected
itzon.tv account joins the open channels and receives or sends their chat.
Switching accounts parts the previous selection and joins the new one while
deduplicating any messages that overlap during the handoff by `msgid`.

## Security and protocol behavior

- Connections use the documented `wss://itzon.tv/ws/irc` endpoint with peer
  certificate verification. Chat bot tokens are never sent over the plaintext
  port.
- On Windows, chat bot tokens and OAuth access/refresh tokens are encrypted
  with DPAPI for the current Windows user before being written to Chatterino's
  settings. Other platforms use base64 encoding, so their settings directory
  must be protected like any other credential store.
- The client negotiates `message-tags`, `echo-message`, `server-time`, and
  `draft/message-redaction`; handles server `PING`, `REDACT`, reconnects, and
  mismatches between chat bot tokens and account names; sends replies using the
  `+reply` tag; and preserves the original timestamps on replayed messages.
- Channel joins, rejoins, parts, disconnects, server notices, authentication
  failures, and rejected joins are shown as timestamped system messages. The
  IRC member list also populates Chatterino's recent-chatter data. Expected
  account-switch handoff parts are suppressed to avoid misleading status
  messages.
- Outgoing chat is queued at a conservative rate below the documented
  approximately two-messages-per-second non-moderator limit. Lines are trimmed
  to the IRC byte limit without splitting Unicode surrogate pairs.
- Global 7TV, BTTV, and FrankerFaceZ emotes from Chatterino7 continue to render.
- For channel 7TV emotes, the client reads `emoteTwitchId` from itzon.tv's
  public `/api/live/channel/{channel}` response and loads the corresponding
  Twitch connection through 7TV's `/v3/users/twitch/{id}` endpoint. This uses
  the Twitch account configured by the channel owner on itzon.tv; no Twitch
  account needs to be configured in Chatterino.
- Channel 7TV emotes work in messages, tab completion, spellcheck exclusion,
  the emote picker and search. **Reload channel emotes** re-resolves the
  itzon.tv mapping, so owner-side changes can be picked up without reopening
  the channel. On startup, JOIN waits for the cached or freshly resolved 7TV
  set (with a five-second safety timeout), so the first replayed messages are
  parsed with channel emotes instead of plain text.

The upstream [Chatterino7 project](https://github.com/SevenTV/chatterino7) and
this fork are MIT-licensed. The itzon.tv protocol behavior follows its
[chat-bot documentation](https://itzon.tv/docs/chat-bots).
