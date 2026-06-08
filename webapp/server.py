#!/usr/bin/env python3
"""Simple HTTPS server for OSLTT webapp"""
import http.server
import ssl
import os

PORT = 8443
CERT_FILE = os.path.join(os.path.dirname(__file__), 'cert.pem')
KEY_FILE = os.path.join(os.path.dirname(__file__), 'key.pem')

handler = http.server.SimpleHTTPRequestHandler
httpd = http.server.HTTPServer(('0.0.0.0', PORT), handler)

ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(CERT_FILE, KEY_FILE)

httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)
print(f"OSLTT Web Controller running on https://0.0.0.0:{PORT}/")
print(f"Open in your browser: https://192.168.1.208:{PORT}/")
print("(Accept the certificate warning)")

try:
    httpd.serve_forever()
except KeyboardInterrupt:
    print("\nShutting down...")
    httpd.server_close()
