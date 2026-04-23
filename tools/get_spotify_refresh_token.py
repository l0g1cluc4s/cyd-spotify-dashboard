#!/usr/bin/env python3
import base64
import http.server
import json
import secrets
import sys
import threading
import time
import urllib.parse
import urllib.request

REDIRECT_URI = "http://127.0.0.1:8888/callback"
SCOPES = "user-read-currently-playing user-read-playback-state user-modify-playback-state"


class CallbackHandler(http.server.BaseHTTPRequestHandler):
    code = None
    state = None
    error = None

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        params = urllib.parse.parse_qs(parsed.query)

        code = params.get("code", [None])[0]
        state = params.get("state", [None])[0]
        error = params.get("error", [None])[0]
        if code or error:
            CallbackHandler.code = code
            CallbackHandler.state = state
            CallbackHandler.error = error

        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.end_headers()
        if code or error:
            self.wfile.write(
                b"<h1>Spotify autorizado</h1><p>Voce pode voltar ao terminal.</p>"
            )
        else:
            self.wfile.write(
                b"<h1>Callback sem codigo</h1>"
                b"<p>Volte para a URL de autorizacao do Spotify e autorize o app.</p>"
            )

    def log_message(self, *_):
        return


def token_request(client_id, client_secret, code):
    body = urllib.parse.urlencode(
        {
            "grant_type": "authorization_code",
            "code": code,
            "redirect_uri": REDIRECT_URI,
        }
    ).encode()
    auth = base64.b64encode(f"{client_id}:{client_secret}".encode()).decode()
    req = urllib.request.Request(
        "https://accounts.spotify.com/api/token",
        data=body,
        headers={
            "Authorization": f"Basic {auth}",
            "Content-Type": "application/x-www-form-urlencoded",
        },
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=20) as response:
        return json.loads(response.read().decode())


def read_code_from_callback_url():
    print("\nSe o navegador abriu uma pagina 127.0.0.1 que nao carregou, copie a URL completa")
    print("da barra de enderecos e cole aqui. Se nao tiver URL de callback, pressione Enter.")
    callback_url = input("URL de callback: ").strip()
    if not callback_url:
        return None, None, None

    parsed = urllib.parse.urlparse(callback_url)
    params = urllib.parse.parse_qs(parsed.query)
    return (
        params.get("code", [None])[0],
        params.get("state", [None])[0],
        params.get("error", [None])[0],
    )


def main():
    if len(sys.argv) != 3:
        print(
            "Uso: python3 tools/get_spotify_refresh_token.py "
            "<SPOTIFY_CLIENT_ID> <SPOTIFY_CLIENT_SECRET>"
        )
        return 2

    client_id = sys.argv[1]
    client_secret = sys.argv[2]
    expected_state = secrets.token_urlsafe(24)
    params = urllib.parse.urlencode(
        {
            "client_id": client_id,
            "response_type": "code",
            "redirect_uri": REDIRECT_URI,
            "scope": SCOPES,
            "state": expected_state,
        }
    )
    auth_url = f"https://accounts.spotify.com/authorize?{params}"

    server = http.server.HTTPServer(("127.0.0.1", 8888), CallbackHandler)

    print("Abra esta URL no navegador e autorize o app:\n")
    print(auth_url)
    print("\nAguardando callback em http://127.0.0.1:8888/callback ...")

    deadline = time.monotonic() + 180
    while time.monotonic() < deadline and not CallbackHandler.code and not CallbackHandler.error:
        thread = threading.Thread(target=server.handle_request, daemon=True)
        thread.start()
        thread.join(timeout=1)
    server.server_close()

    if CallbackHandler.error:
        print(f"Erro retornado pelo Spotify: {CallbackHandler.error}")
        return 1
    if not CallbackHandler.code:
        print("Tempo esgotado sem receber o codigo de autorizacao automaticamente.")
        (
            CallbackHandler.code,
            CallbackHandler.state,
            CallbackHandler.error,
        ) = read_code_from_callback_url()
        if CallbackHandler.error:
            print(f"Erro retornado pelo Spotify: {CallbackHandler.error}")
            return 1
        if not CallbackHandler.code:
            print("Nenhum codigo de autorizacao recebido.")
            return 1
    if CallbackHandler.state != expected_state:
        print("State invalido no callback. Abortando.")
        return 1

    tokens = token_request(client_id, client_secret, CallbackHandler.code)
    refresh_token = tokens.get("refresh_token")
    if not refresh_token:
        print(json.dumps(tokens, indent=2))
        print("A resposta nao trouxe refresh_token.")
        return 1

    print("\nSPOTIFY_REFRESH_TOKEN:")
    print(refresh_token)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
