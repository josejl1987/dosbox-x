import sqlite3
conn = sqlite3.connect('pc98rev.db')
c = conn.cursor()
print('tables:', [t[0] for t in c.execute("SELECT name FROM sqlite_master WHERE type='table'")])
print('event types:')
for row in c.execute("SELECT type, COUNT(*) FROM events GROUP BY type ORDER BY 2 DESC"):
    print(row)
