#!/usr/bin/env python3

import sys
import urllib.parse
from pathlib import Path

def emit_headers():
    sys.stdout.write("Content-Type: text/html\r\n\r\n")

def main():
    raw_input = sys.stdin.read()
    form_data = urllib.parse.parse_qs(raw_input)
    name = form_data.get('name', [''])[0]
    email = form_data.get('email', [''])[0]
    message = form_data.get('message', [''])[0]

    submissions = Path("server_2/forms/submissions.txt")
    submissions.parent.mkdir(parents=True, exist_ok=True)
    with submissions.open("a") as f:
        f.write(f"Name: {name}\nEmail: {email}\nMessage: {message}\n---\n\n")

    emit_headers()
    print("""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <link rel="stylesheet" href="/style.css">
  <title>Form submitted</title>
</head>
<body>
  <main style="display:flex;justify-content:center;align-items:center;min-height:100vh;">
    <section class="card" style="max-width:520px;text-align:center;">
      <h2>Form Submitted Successfully</h2>
      <p class="mb-4">Thank you, """ + (name or "friend") + """!</p>
      <a class="btn" href="/index.html">Back to home</a>
    </section>
  </main>
</body>
</html>""")

if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        emit_headers()
        print("<h1>Form Submission Failed</h1>")
        print(f"<pre>{e}</pre>")
