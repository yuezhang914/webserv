
#!/usr/bin/env python3
import os

print("Content-Type: text/html\r\n\r\n", end="")
print("<h1>PATH_INFO Test Success!</h1>")
print(f"<p><b>SCRIPT_NAME:</b> {os.environ.get('SCRIPT_NAME')}</p>")
print(f"<p><b>PATH_INFO:</b> {os.environ.get('PATH_INFO')}</p>")
print(f"<p><b>PATH_TRANSLATED:</b> {os.environ.get('PATH_TRANSLATED')}</p>")
EOF