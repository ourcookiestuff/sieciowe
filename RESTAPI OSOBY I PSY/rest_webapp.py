import re, sqlite3
from urllib.parse import parse_qs

plik_bazy = './osoby.sqlite'

class OsobyApp:
    def __init__(self, environment, start_response):
        self.env = environment
        self.start_response = start_response
        self.status = '200 OK'
        self.headers = [ ('Content-Type', 'text/html; charset=UTF-8') ]
        self.content = b''

    def __iter__(self):
        try:
            self.route()
        except sqlite3.Error as e:
            s = 'SQLite error: ' + str(e)
            self.failure('500 Internal Server Error', s)
        n = len(self.content)
        self.headers.append( ('Content-Length', str(n)) )
        self.start_response(self.status, self.headers)
        yield self.content

    def failure(self, status, detail = None):
        self.status = status
        s = '<html>\n<head>\n<title>' + status + '</title>\n</head>\n'
        s += '<body>\n<h1>' + status + '</h1>\n'
        if detail is not None:
            s += '<p>' + detail + '</p>\n'
        s += '</body>\n</html>\n'
        self.content = s.encode('UTF-8')

    def route(self):
        path = self.env['PATH_INFO']
        if path == '/osoby':    # Wszystkie osoby (GET/POST)
            self.handle_table('osoby')
        elif path.startswith('/osoby/search'):  # Wyszukiwanie po imieniu/nazwisku
            self.handle_search()
        elif m := re.match('^/osoby/(\d+)$', path): # Osoba o konkretnym id
            self.handle_item('osoby', int(m.group(1)))
        elif path == '/psy':    # Wszystkie psy (GET/POST)
            self.handle_table('psy')
        elif path.startswith('/psy/search'): # Wyszukiwanie po imieniu/rasie
            self.handle_search_psy()
        elif m := re.match('^/psy/(\d+)$', path):   # Pies o konkretnym id
            self.handle_item('psy', int(m.group(1)))
        else:
            self.failure('404 Not Found')

    def handle_search(self):
        qs = parse_qs(self.env.get('QUERY_STRING', ''))
        imie = qs.get('imie', [None])[0]
        nazwisko = qs.get('nazwisko', [None])[0]
        if not imie and not nazwisko:
            self.failure('400 Bad Request', 'Brak parametrów wyszukiwania')
            return
        conditions = [] # warunki SQL WHERE
        params = [] # wartości do podstawienia w miejsce `?`
        if imie:
            conditions.append("imie = ?")
            params.append(imie)
        if nazwisko:
            conditions.append("nazwisko = ?")
            params.append(nazwisko)
        where = " WHERE " + " AND ".join(conditions) if conditions else ""
        q = "SELECT * FROM osoby" + where

        try:
            with sqlite3.connect(plik_bazy) as conn:
                cur = conn.cursor()
                cur.execute(q, params)
                rows = cur.fetchall()
                if not rows:
                    self.failure('404 Not Found', 'Brak danych')
                    return
                colnames = [d[0] for d in cur.description]
                self.send_rows(colnames, rows)

        except Exception as e:
            self.failure('500 Internal Server Error', f'Błąd zapytania: {e}')

    def handle_search_psy(self):
        qs = parse_qs(self.env.get('QUERY_STRING', ''))
        imie = qs.get('imie', [None])[0]
        rasa = qs.get('rasa', [None])[0]
        if not imie and not rasa:
            self.failure('400 Bad Request', 'Brak parametrów wyszukiwania')
            return
        conditions = [] # warunki SQL WHERE
        params = [] # wartości do podstawienia w miejsce `?`
        if imie:
            conditions.append("imie = ?")
            params.append(imie)
        if rasa:
            conditions.append("rasa = ?")
            params.append(rasa)
        where = " WHERE " + " AND ".join(conditions)
        q = "SELECT * FROM psy" + where

        try:
            with sqlite3.connect(plik_bazy) as conn:
                cur = conn.cursor()
                cur.execute(q, params)
                rows = cur.fetchall()
                if not rows:
                    self.failure('404 Not Found', 'Brak danych')
                    return
                colnames = [d[0] for d in cur.description]
                self.send_rows(colnames, rows)
        except Exception as e:
            self.failure('500 Internal Server Error', f'Błąd zapytania: {e}')

    # Obsługa żądań GET/POST/PUT/DELETE

    def owner_exists(self, owner_id):
        try:
            with sqlite3.connect(plik_bazy) as conn:
                cur = conn.cursor()
                cur.execute("SELECT 1 FROM osoby WHERE id = ?", (owner_id,))
                return cur.fetchone() is not None
        except Exception as e:
            self.failure('500 Internal Server Error', f'Błąd sprawdzania właściciela: {e}')
            return False

    def handle_table(self, table):
        method = self.env['REQUEST_METHOD']
        if method == 'GET':
            self.sql_select(table)
        elif method == 'POST':
            colnames, vals = self.read_tsv()
            if colnames is None:
                return

            if table == 'psy':
                try:
                    owner_index = colnames.index('wlasciciel_id')
                    owner_id = vals[owner_index]
                    if not self.owner_exists(owner_id):
                        self.failure('400 Bad Request', f'Nie istnieje właściciel o id={owner_id}')
                        return
                except (ValueError, IndexError) as e:
                    self.failure('400 Bad Request', 'Brak kolumny wlasciciel_id lub błędna wartość')
                    return

            q = f'INSERT INTO {table} ({", ".join(colnames)}) VALUES ({", ".join(["?"]*len(vals))})'
            id = self.sql_modify(q, vals)
            self.sql_select(table, id)
        else:
            self.failure('501 Not Implemented')
        
    def record_exists(self, table, id):
        try:
            with sqlite3.connect(plik_bazy) as conn:
                cur = conn.cursor()
                cur.execute(f"SELECT 1 FROM {table} WHERE id = ?", (id,))
                return cur.fetchone() is not None
        except Exception as e:
            self.failure('500 Internal Server Error', f'Błąd sprawdzania rekordu: {e}')
            return False
    
    def person_has_dogs(self, person_id):
        try:
            with sqlite3.connect(plik_bazy) as conn:
                cur = conn.cursor()
                cur.execute("SELECT COUNT(*) FROM psy WHERE wlasciciel_id = ?", (person_id,))
                result = cur.fetchone()[0]
                return result > 0
        except Exception as e:
            self.failure('500 Internal Server Error', f'Błąd sprawdzania właściciela psa: {e}')
            return False

    def handle_item(self, table, id):
        method = self.env['REQUEST_METHOD']
        if method == 'GET':
            self.sql_select(table, id)
        elif method == 'PUT':
            if not self.record_exists(table, id):
                self.failure('404 Not Found', f'Rekord o id={id} w tabeli "{table}" nie istnieje')
                return
            colnames, vals = self.read_tsv()
            q = f'UPDATE {table} SET ' + ', '.join([f'{c} = ?' for c in colnames]) + f' WHERE id = {id}'
            self.sql_modify(q, vals)
            self.sql_select(table, id)
        elif method == 'DELETE':
            if not self.record_exists(table, id):
                self.failure('404 Not Found', f'Rekord o id={id} w tabeli "{table}" nie istnieje')
                return
            if table == 'osoby':
                if self.person_has_dogs(id):
                    self.failure('409 Conflict', 'Osoba ma przypisanego psa.')
                    return
            q = f'DELETE FROM {table} WHERE id = {id}'
            self.sql_modify(q)
        else:
            self.failure('501 Not Implemented')

    # Odczyt danych z żądania

    def read_tsv(self):
        try:
            f = self.env['wsgi.input']
            n = int(self.env['CONTENT_LENGTH'])
            if n <= 0:
                raise ValueError("Brak danych (CONTENT_LENGTH = 0)")
            raw_bytes = f.read(n)
            lines = raw_bytes.decode('UTF-8').splitlines()
            if len(lines) < 2:
                raise ValueError("Brak wymaganych dwóch linii TSV (nagłówki i dane)")
            colnames = lines[0].split('\t')
            vals = lines[1].split('\t')
            if len(colnames) != len(vals):
                raise ValueError("Liczba kolumn i wartości się nie zgadza")
            return colnames, vals
        except Exception as e:
            self.failure('400 Bad Request', f'Błąd przetwarzania TSV: {e}')
            return None, None
        
    # Wysyłanie danych (GET), formatuje dane jako TSV 

    def send_rows(self, colnames, rows):
        try:
            s = '\t'.join(colnames) + '\n'
            for row in rows:
                s += '\t'.join(str(v) if v is not None else '' for v in row) + '\n'
            self.content = s.encode('UTF-8')
            self.headers = [('Content-Type', 'text/tab-separated-values; charset=UTF-8')]
        except Exception as e:
            self.failure('500 Internal Server Error', f'Błąd formatowania danych: {e}')
    
    # Zapytania SQL

    def sql_select(self, table, id=None):
        try:
            with sqlite3.connect(plik_bazy) as conn:
                cur = conn.cursor()
                q = f"SELECT * FROM {table}"
                if id is not None:
                    q += f" WHERE id = ?"
                    cur.execute(q, (id,))
                else:
                    cur.execute(q)
                colnames = [d[0] for d in cur.description]
                rows = cur.fetchall()
                self.send_rows(colnames, rows)
        except Exception as e:
            self.failure('500 Internal Server Error', f'Błąd zapytania SELECT: {e}')
            return [], []

    def sql_modify(self, query, params = None):
        try:
            with sqlite3.connect(plik_bazy) as conn:
                crsr = conn.cursor()
                if params is None:
                    crsr.execute(query)
                else:
                    crsr.execute(query, params)
                rowid = crsr.lastrowid   # id wiersza wstawionego przez INSERT
                conn.commit()
                return rowid
        except Exception as e:
            self.failure('500 Internal Server Error', f'Błąd modyfikacji bazy: {e}')
            return -1

if __name__ == '__main__':
    from wsgiref.simple_server import make_server
    port = 8000
    httpd = make_server('', port, OsobyApp)
    print('Listening on port %i, press ^C to stop.' % port)
    httpd.serve_forever()
