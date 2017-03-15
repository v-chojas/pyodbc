
from datetime import datetime
from decimal import Decimal

from testutils import *
add_to_path()

import pyodbc

connection_string = load_setup_connection_string('pgtests')
cnxn = pyodbc.connect(connection_string)
cursor = cnxn.cursor()

cursor.execute("drop table if exists t" )
cursor.execute("create table t(id int, s varchar(1000), t timestamp, n numeric(10,3))")
cursor.commit()

def t1():
    for i in range(100):
        cursor.execute("insert into t(id, s) values (?, ?)", i, str(i))
    cursor.commit()

def t2():
    for i in range(100):
        row = cursor.execute("select * from t where id=?", i).fetchone()

def t3():
    cursor.execute("insert into t(t) values (?)", datetime.now())
    row = cursor.execute("select t from t where t is not null").fetchone()
    print('row:', row)

    cursor.execute("insert into t(n) values (?)", Decimal("123.45"))
    row = cursor.execute("select n from t where n is not null").fetchone()
    print('row:', row)

# t1()
# t2()
t3()

cursor = None
cnxn = None

pyodbc.leakcheck()
