
import http.client

HOST = "127.0.0.1"
PORT = 8080

# 1. Testwerte
data = "temperature=25.5&humidity=60&light=450&smoke=12"

# 2. Verbindung herstellen
conn = http.client.HTTPConnection(HOST, PORT, timeout=5)

# 3. POST senden
conn.request(
    "POST",
    "/sensor",
    body=data,
    headers={"Content-Type": "application/x-www-form-urlencoded"}
)

response = conn.getresponse()
response.read()

assert response.status == 200, "POST fehlgeschlagen"

# 4. GET senden
conn.request("GET", "/sensor")

response = conn.getresponse()
html = response.read().decode("utf-8")

assert response.status == 200, "GET fehlgeschlagen"

# 5. Gespeicherte Werte überprüfen
assert "25.50" in html, "Temperatur falsch gespeichert"
assert "60.00" in html, "Luftfeuchtigkeit falsch gespeichert"
assert "450.00" in html, "Helligkeit falsch gespeichert"
assert "12.00" in html, "Rauchwert falsch gespeichert"

print("TEST BESTANDEN: Alle Sensorwerte korrekt gespeichert!")

conn.close()

