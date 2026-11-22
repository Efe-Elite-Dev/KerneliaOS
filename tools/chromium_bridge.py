#!/usr/bin/env python3
"""WindOS Chromium koprusu (host tarafi).

WindOS, QEMU'nun user-mode agi uzerinden 10.0.2.2:8088 adresine baglanir.
Bu servis istenen sayfayi host makinedeki Chromium ile acar ve sayfanin
ekran goruntusunu 24-bit BMP olarak geri yollar. Boylece JavaScript ile
calisan siteler (ornegin YouTube) gorunur ve DNS cozumlemesi host tarafinda
yapilir. Servis yalnizca 127.0.0.1 uzerinde dinler; QEMU 10.0.2.2 adresini
bu loopback'e yonlendirir, dolayisiyla ag uzerinden erisilemez.

Kurulum (bir kez):
    pip install playwright pillow
    python -m playwright install chromium

Calistirma:
    python tools/chromium_bridge.py              (varsayilan 127.0.0.1:8088, gorunur Chromium penceresi)
    python tools/chromium_bridge.py --headless   (pencere acmadan; ses calmaz)
    python tools/chromium_bridge.py --port 9000  (WindOS tarafinda BR_PORT da degismeli)

Gorunur Chromium, sesi bilgisayarin hoparlorunden calar; WindOS sadece goruntuyu alir.

HTTP protokolu (HTTP/1.0):
    GET /act?a=go&u=<adres>&w=<px>&h=<px>        adresi ac
    GET /act?a=scroll&dy=<px>&w=<px>&h=<px>      sayfayi kaydir (pozitif = asagi)
    GET /act?a=click&x=<px>&y=<px>&w=<px>&h=<px> gorunen alanda (viewport) tikla
    GET /act?a=key&k=<tus>&w=<px>&h=<px>         tus bas (Space, ArrowLeft, ArrowRight, ...)
    GET /act?a=frame&w=<px>&h=<px>               sayfanin simdiki karesini al (video icin)

Basarili yanit: 200, Content-Type image/bmp, X-Url: gecerli adres.
Hata yaniti: 502, text/plain olarak aciklama.
"""
import argparse
import io
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlparse, urlsplit, urlunsplit

from PIL import Image
from playwright.sync_api import sync_playwright

DEFAULT_W, DEFAULT_H = 720, 408
KEY_RE = re.compile(r"^[A-Za-z]{1,12}$")  # yalnizca Playwright tus adlari (Space, ArrowLeft, ...)
GUEST_ALIAS = "10.0.2.2"  # QEMU user-mode agi: host makinenin loopback'i


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def normalize(url):
    """Sema yoksa http:// ekler; WindOS'un 10.0.2.2 adresini host loopback'ine cevirir."""
    url = url.strip()
    if not re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*://", url):
        url = "http://" + url
    parts = urlsplit(url)
    if parts.hostname == GUEST_ALIAS:
        port = f":{parts.port}" if parts.port else ""
        parts = parts._replace(netloc="127.0.0.1" + port)
        url = urlunsplit(parts)
    return url


class Session:
    """Tek bir Chromium sayfasi; WindOS'un tarayici penceresiyle eslesir."""

    def __init__(self, headless=False):
        self._pw = sync_playwright().start()
        # Video otomatik baslasin diye kullanici hareketi beklenmez
        self._browser = self._pw.chromium.launch(
            headless=headless,
            args=["--autoplay-policy=no-user-gesture-required"])
        self._ctx = self._browser.new_context(
            viewport={"width": DEFAULT_W, "height": DEFAULT_H})
        self.page = self._ctx.new_page()
        self.w, self.h = DEFAULT_W, DEFAULT_H

    def close(self):
        self._browser.close()
        self._pw.stop()

    def set_size(self, w, h):
        if (w, h) != (self.w, self.h):
            self.page.set_viewport_size({"width": w, "height": h})
            self.w, self.h = w, h

    def _settle(self):
        try:
            self.page.wait_for_load_state("load", timeout=6000)
        except Exception:
            pass
        time.sleep(0.6)  # JavaScript ile sonradan gelen icerik icin kisa bekleme

    def go(self, url):
        self.page.goto(normalize(url), wait_until="domcontentloaded", timeout=15000)
        self._settle()

    def scroll(self, dy):
        self.page.mouse.move(self.w / 2, self.h / 2)
        self.page.mouse.wheel(0, dy)
        time.sleep(0.5)

    def click(self, x, y):
        self.page.mouse.click(x, y)
        time.sleep(0.4)
        self._settle()

    def key(self, name):
        self.page.keyboard.press(name)
        time.sleep(0.2)

    def bmp(self):
        png = self.page.screenshot(type="png")
        buf = io.BytesIO()
        Image.open(io.BytesIO(png)).convert("RGB").save(buf, "BMP")
        return buf.getvalue()

    @property
    def url(self):
        return self.page.url


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    session = None  # main() icinde atanir

    def log_message(self, fmt, *args):
        sys.stderr.write("[kopru] " + (fmt % args) + "\n")

    def _send(self, code, ctype, body, extra=None):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path != "/act":
            return self._send(404, "text/plain; charset=utf-8", b"404 bulunamadi")
        q = {k: v[0] for k, v in parse_qs(parsed.query).items()}
        s = Handler.session
        try:
            w = clamp(int(q.get("w", DEFAULT_W)), 160, 1280)
            h = clamp(int(q.get("h", DEFAULT_H)), 120, 1024)
            s.set_size(w, h)
            action = q.get("a", "")
            if action == "go":
                s.go(q.get("u", ""))
            elif action == "scroll":
                s.scroll(clamp(int(q.get("dy", 0)), -4000, 4000))
            elif action == "click":
                s.click(clamp(int(q["x"]), 0, w), clamp(int(q["y"]), 0, h))
            elif action == "key":
                name = q.get("k", "")
                if not KEY_RE.match(name):
                    raise ValueError("gecersiz tus: " + name[:20])
                s.key(name)
            elif action == "frame":
                pass  # yalnizca simdiki kareyi al
            else:
                raise ValueError("bilinmeyen islem: " + action)
            body = s.bmp()
        except Exception as exc:  # Playwright ve ayristirma hatalari WindOS'a metin olarak gider
            msg = ("HATA: " + str(exc).splitlines()[0] if str(exc) else "HATA").encode("utf-8", "replace")
            return self._send(502, "text/plain; charset=utf-8", msg)
        url_hdr = s.url.encode("ascii", "replace").decode("ascii")[:200]
        self._send(200, "image/bmp", body, {"X-Url": url_hdr})


def main():
    ap = argparse.ArgumentParser(description="WindOS Chromium koprusu")
    ap.add_argument("--port", type=int, default=8088)
    ap.add_argument("--bind", default="127.0.0.1")
    ap.add_argument("--headless", action="store_true",
                    help="Chromium penceresi acma (ses calmaz, test icin)")
    args = ap.parse_args()

    Handler.session = Session(headless=args.headless)
    server = HTTPServer((args.bind, args.port), Handler)
    print(f"WindOS Chromium koprusu: {args.bind}:{args.port} "
          f"(WindOS icinden 10.0.2.2:{args.port}). Durdurmak icin Ctrl+C.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        Handler.session.close()


if __name__ == "__main__":
    main()
