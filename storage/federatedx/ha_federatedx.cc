/*
Copyright (c) 2008-2009, Patrick Galbraith & Antony Curtis
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

    * Neither the name of Patrick Galbraith nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/*

  FederatedX Pluggable Storage Engine

  ha_federatedx.cc - FederatedX Pluggable Storage Engine
  Patrick Galbraith, 2008

  This is a handler which uses a foreign database as the data file, as
  opposed to a handler like MyISAM, which uses .MYD files locally.

  How this handler works
  ----------------------------------
  Normal database files are local and as such: You create a table called
  'users', a file such as 'users.MYD' is created. A handler reads, inserts,
  deletes, updates data in this file. The data is stored in particular format,
  so to read, that data has to be parsed into fields, to write, fields have to
  be stored in this format to write to this data file.

  With FederatedX storage engine, there will be no local files
  for each table's data (such as .MYD). A foreign database will store
  the data that would normally be in this file. This will necessitate
  the use of MySQL client API to read, delete, update, insert this
  data. The data will have to be retrieve via an SQL call "SELECT *
  FROM users". Then, to read this data, it will have to be retrieved
  via mysql_fetch_row one row at a time, then converted from the
  column in this select into the format that the handler expects.

  The create table will simply create the .frm file, and within the
  "CREATE TABLE" SQL, there SHALL be any of the following :

  connection=scheme://username:password@hostname:port/database/tablename
  connection=scheme://username@hostname/database/tablename
  connection=scheme://username:password@hostname/database/tablename
  connection=scheme://username:password@hostname/database/tablename

  - OR -

  As of 5.1 federatedx now allows you to use a non-url
  format, taking advantage of mysql.servers:

  connection="connection_one"
  connection="connection_one/table_foo"

  An example would be:

  connection=mysql://username:password@hostname:port/database/tablename

  or, if we had:

  create server 'server_one' foreign data wrapper 'mysql' options
  (HOST '127.0.0.1',
  DATABASE 'db1',
  USER 'root',
  PASSWORD '',
  PORT 3306,
  SOCKET '',
  OWNER 'root');

  CREATE TABLE federatedx.t1 (
    `id` int(20) NOT NULL,
    `name` varchar(64) NOT NULL default ''
    )
  ENGINE="FEDERATEDX" DEFAULT CHARSET=latin1
  CONNECTION='server_one';

  So, this will have been the equivalent of

  CONNECTION="mysql://root@127.0.0.1:3306/db1/t1"

  Then, we can also change the server to point to a new schema:

  ALTER SERVER 'server_one' options(DATABASE 'db2');

  All subsequent calls will now be against db2.t1! Guess what? You don't
  have to perform an alter table!

  This connecton="connection string" is necessary for the handler to be
  able to connect to the foreign server, either by URL, or by server
  name. 


  The basic flow is this:

  SQL calls issues locally ->
  mysql handler API (data in handler format) ->
  mysql client API (data converted to SQL calls) ->
  foreign database -> mysql client API ->
  convert result sets (if any) to handler format ->
  handler API -> results or rows affected to local

  What this handler does and doesn't support
  ------------------------------------------
  * Tables MUST be created on the foreign server prior to any action on those
    tables via the handler, first version. IMPORTANT: IF you MUST use the
    federatedx storage engine type on the REMOTE end, MAKE SURE [ :) ] That
    the table you connect to IS NOT a table pointing BACK to your ORIGNAL
    table! You know  and have heard the screaching of audio feedback? You
    know putting two mirror in front of each other how the reflection
    continues for eternity? Well, need I say more?!
  * There will not be support for transactions.
  * There is no way for the handler to know if the foreign database or table
    has changed. The reason for this is that this database has to work like a
    data file that would never be written to by anything other than the
    database. The integrity of the data in the local table could be breached
    if there was any change to the foreign database.
  * Support for SELECT, INSERT, UPDATE , DELETE, indexes.
  * No ALTER TABLE, DROP TABLE or any other Data Definition Language calls.
  * Prepared statements will not be used in the first implementation, it
    remains to to be seen whether the limited subset of the client API for the
    server supports this.
  * This uses SELECT, INSERT, UPDATE, DELETE and not HANDLER for its
    implementation.
  * This will not work with the query cache.

   Method calls

   A two column table, with one record:

   (SELECT)

   "SELECT * FROM foo"
    ha_federatedx::info
    ha_federatedx::scan_time:
    ha_federatedx::rnd_init: share->select_query SELECT * FROM foo
    ha_federatedx::extra

    <for every row of data retrieved>
    ha_federatedx::rnd_next
    ha_federatedx::convert_row_to_internal_format
    ha_federatedx::rnd_next
    </for every row of data retrieved>

    ha_federatedx::rnd_end
    ha_federatedx::extra
    ha_federatedx::reset

    (INSERT)

    "INSERT INTO foo (id, ts) VALUES (2, now());"

    ha_federatedx::write_row

    ha_federatedx::reset

    (UPDATE)

    "UPDATE foo SET ts = now() WHERE id = 1;"

    ha_federatedx::index_init
    ha_federatedx::index_read
    ha_federatedx::index_read_idx
    ha_federatedx::rnd_next
    ha_federatedx::convert_row_to_internal_format
    ha_federatedx::update_row

    ha_federatedx::extra
    ha_federatedx::extra
    ha_federatedx::extra
    ha_federatedx::external_lock
    ha_federatedx::reset


    How do I use this handler?
    --------------------------

    <insert text about plugin storage engine>

    Next, to use this handler, it's very simple. You must
    have two databases running, either both on the same host, or
    on different hosts.

    One the server that will be connecting to the foreign
    host (client), you create your table as such:

    CREATE TABLE test_table (
      id     int(20) NOT NULL auto_increment,
      name   varchar(32) NOT NULL default '',
      other  int(20) NOT NULL default '0',
      PRIMARY KEY  (id),
      KEY name (name),
      KEY other_key (other))
       ENGINE="FEDERATEDX"
       DEFAULT CHARSET=latin1
       CONNECTION='mysql://root@127.0.0.1:9306/federatedx/test_federatedx';

   Notice the "COMMENT" and "ENGINE" field? This is where you
   respectively set the engine type, "FEDERATEDX" and foreign
   host information, this being the database your 'client' database
   will connect to and use as the "data file". Obviously, the foreign
   database is running on port 9306, so you want to start up your other
   database so that it is indeed on port 9306, and your federatedx
   database on a port other than that. In my setup, I use port 5554
   for federatedx, and port 5555 for the foreign database.

   Then, on the foreign database:

   CREATE TABLE test_table (
     id     int(20) NOT NULL auto_increment,
     name   varchar(32) NOT NULL default '',
     other  int(20) NOT NULL default '0',
     PRIMARY KEY  (id),
     KEY name (name),
     KEY other_key (other))
     ENGINE="<NAME>" <-- whatever you want, or not specify
     DEFAULT CHARSET=latin1 ;

    This table is exactly the same (and must be exactly the same),
    except that it is not using the federatedx handler and does
    not need the URL.


    How to see the handler in action
    --------------------------------

    When developing this handler, I compiled the federatedx database with
    debugging:

    ./configure --with-federatedx-storage-engine
    --prefix=/home/mysql/mysql-build/federatedx/ --with-debug

    Once compiled, I did a 'make install' (not for the purpose of installing
    the binary, but to install all the files the binary expects to see in the
    diretory I specified in the build with --prefix,
    "/home/mysql/mysql-build/federatedx".

    Then, I started the foreign server:

    /usr/local/mysql/bin/mysqld_safe
    --user=mysql --log=/tmp/mysqld.5555.log -P 5555

    Then, I went back to the directory containing the newly compiled mysqld,
    <builddir>/sql/, started up gdb:

    gdb ./mysqld

    Then, withn the (gdb) prompt:
    (gdb) run --gdb --port=5554 --socket=/tmp/mysqld.5554 --skip-innodb --debug-dbug

    Next, I open several windows for each:

    1. Tail the debug trace: tail -f /tmp/mysqld.trace|grep ha_fed
    2. Tail the SQL calls to the foreign database: tail -f /tmp/mysqld.5555.log
    3. A window with a client open to the federatedx server on port 5554
    4. A window with a client open to the federatedx server on port 5555

    I would create a table on the client to the foreign server on port
    5555, and then to the federatedx server on port 5554. At this point,
    I would run whatever queries I wanted to on the federatedx server,
    just always remembering that whatever changes I wanted to make on
    the table, or if I created new tables, that I would have to do that
    on the foreign server.

    Another thing to look for is 'show variables' to show you that you have
    support for federatedx handler support:

    show variables like '%federat%'

    and:

    show storage engines;

    Both should display the federatedx storage handler.


    Testing
    -------

    Testing for FederatedX as a pluggable storage engine for
    now is a manual process that I intend to build a test
    suite that works for all pluggable storage engines.

    How to test

    1. cp fed.dat /tmp
    (make sure you have access to "test". Use a user that has
    super privileges for now)
    2. mysql -f -u root test < federated.test > federated.myresult 2>&1
    3. diff federated.result federated.myresult (there _should_ be no differences)


*/

#ifdef USE_PRAGMA_IMPLEMENTATION
#pragma implementation                          // gcc: Class implementation
#endif

#define MYSQL_SERVER 1
#include <my_global.h>
#include <mysql/plugin.h>
#include <sql_select.h>
//#include <sql_test.h>
#include "ha_federatedx.h"
#include "sql_servers.h"
#include "sql_analyse.h"                        // append_escaped()
#include "sql_show.h"                           // append_identifier()
#include "tztime.h"                             // my_tz_find()

#ifdef I_AM_PARANOID
#define MIN_PORT 1023
#else
#define MIN_PORT 0
#endif

/* Variables for federatedx share methods */
static HASH federatedx_open_tables;             // To track open tables
static HASH federatedx_open_servers;            // To track open servers
mysql_mutex_t federatedx_mutex;                 // To init the hash
const char ident_quote_char= '`';               // Character for quoting
                                                // identifiers
const char value_quote_char= '\'';              // Character for quoting
                                                // literals
static const int bulk_padding= 64;              // bytes "overhead" in packet

/* Variables used when chopping off trailing characters */
static const uint sizeof_trailing_comma= sizeof(", ") - 1;
static const uint sizeof_trailing_and= sizeof(" AND ") - 1;
static const uint sizeof_trailing_where= sizeof(" WHERE ") - 1;

static Time_zone *UTC= 0;

/* Static declaration for handerton */
static handler *federatedx_create_handler(handlerton *hton,
                                         TABLE_SHARE *table,
                                         MEM_ROOT *mem_root);

/* FederatedX storage engine handlerton */

static handler *federatedx_create_handler(handlerton *hton, 
                                         TABLE_SHARE *table,
                                         MEM_ROOT *mem_root)
{
  return new (mem_root) ha_federatedx(hton, table);
}


/* Function we use in the creation of our hash to get key */

static uchar *
federatedx_share_get_key(FEDERATEDX_SHARE *share, size_t *length,
                         my_bool not_used __attribute__ ((unused)))
{
  *length= share->share_key_length;
  return (uchar*) share->share_key;
}


static uchar *
federatedx_server_get_key(FEDERATEDX_SERVER *server, size_t *length,
                          my_bool not_used __attribute__ ((unused)))
{
  *length= server->key_length;
  return server->key;
}

#ifdef HAVE_PSI_INTERFACE
static PSI_mutex_key fe_key_mutex_federatedx, fe_key_mutex_FEDERATEDX_SERVER_mutex;

static PSI_mutex_info all_federated_mutexes[]=
{
  { &fe_key_mutex_federatedx, "federatedx", PSI_FLAG_GLOBAL},
  { &fe_key_mutex_FEDERATEDX_SERVER_mutex, "FEDERATED_SERVER::mutex", 0}
};

static void init_federated_psi_keys(void)
{
  const char* category= "federated";
  int count;

  if (PSI_server == NULL)
    return;

  count= array_elements(all_federated_mutexes);
  PSI_server->register_mutex(category, all_federated_mutexes, count);
}
#else
#define init_federated_psi_keys() /* no-op */
#endif /* HAVE_PSI_INTERFACE */


/*
  Initialize the federatedx handler.

  SYNOPSIS
    federatedx_db_init()
    p		Handlerton

  RETURN
    FALSE       OK
    TRUE        Error
*/

int federatedx_db_init(void *p)
{
  DBUG_ENTER("federatedx_db_init");
  init_federated_psi_keys();
  handlerton *federatedx_hton= (handlerton *)p;
  federatedx_hton->state= SHOW_OPTION_YES;
  /* Needed to work with old .frm files */
  federatedx_hton->db_type= DB_TYPE_FEDERATED_DB;
  federatedx_hton->savepoint_offset= sizeof(ulong);
  federatedx_hton->close_connection= ha_federatedx::disconnect;
  federatedx_hton->savepoint_set= ha_federatedx::savepoint_set;
  federatedx_hton->savepoint_rollback= ha_federatedx::savepoint_rollback;
  federatedx_hton->savepoint_release= ha_federatedx::savepoint_release;
  federatedx_hton->commit= ha_federatedx::commit;
  federatedx_hton->rollback= ha_federatedx::rollback;
  federatedx_hton->discover_table_structure= ha_federatedx::discover_assisted;
  federatedx_hton->create= federatedx_create_handler;
  federatedx_hton->flags= HTON_ALTER_NOT_SUPPORTED;

  if (mysql_mutex_init(fe_key_mutex_federatedx,
                       &federatedx_mutex, MY_MUTEX_INIT_FAST))
    goto error;
  if (!my_hash_init(&federatedx_open_tables, &my_charset_bin, 32, 0, 0,
                 (my_hash_get_key) federatedx_share_get_key, 0, 0) &&
      !my_hash_init(&federatedx_open_servers, &my_charset_bin, 32, 0, 0,
                 (my_hash_get_key) federatedx_server_get_key, 0, 0))
  {
    DBUG_RETURN(FALSE);
  }

  mysql_mutex_destroy(&federatedx_mutex);
error:
  DBUG_RETURN(TRUE);
}


/*
  Release the federatedx handler.

  SYNOPSIS
    federatedx_db_end()

  RETURN
    FALSE       OK
*/

int federatedx_done(void *p)
{
  my_hash_free(&federatedx_open_tables);
  my_hash_free(&federatedx_open_servers);
  mysql_mutex_destroy(&federatedx_mutex);

  return 0;
}

/**
  @brief Append identifiers to the string.

  @param[in,out] string	The target string.
  @param[in] name 		Identifier name
  @param[in] length 	Length of identifier name in bytes
  @param[in] quote_char Quote char to use for quoting identifier.

  @return Operation Status
  @retval FALSE OK
  @retval TRUE  There was an error appending to the string.

  @note This function is based upon the append_identifier() function
        in sql_show.cc except that quoting always occurs.
*/

bool append_ident(String *string, const char *name, size_t length,
                  const char quote_char)
{
  bool result;
  uint clen;
  const char *name_end;
  DBUG_ENTER("append_ident");

  if (quote_char)
  {
    string->reserve(length * 2 + 2);
    if ((result= string->append(&quote_char, 1, system_charset_info)))
      goto err;

    for (name_end= name+length; name < name_end; name+= clen)
    {
      uchar c= *(uchar *) name;
      clen= my_charlen_fix(system_charset_info, name, name_end);
      if (clen == 1 && c == (uchar) quote_char &&
          (result= string->append(&quote_char, 1, system_charset_info)))
        goto err;
      if ((result= string->append(name, clen, string->charset())))
        goto err;
    }
    result= string->append(&quote_char, 1, system_charset_info);
  }
  else
    result= string->append(name, length, system_charset_info);

err:
  DBUG_RETURN(result);
}


static int parse_url_error(FEDERATEDX_SHARE *share, TABLE_SHARE *table_s,
                           int error_num)
{
  char buf[FEDERATEDX_QUERY_BUFFER_SIZE];
  size_t buf_len;
  DBUG_ENTER("ha_federatedx parse_url_error");

  buf_len= MY_MIN(table_s->connect_string.length,
                  FEDERATEDX_QUERY_BUFFER_SIZE-1);
  strmake(buf, table_s->connect_string.str, buf_len);
  my_error(error_num, MYF(0), buf, 14);
  DBUG_RETURN(error_num);
}

/*
  retrieve server object which contains server meta-data 
  from the system table given a server's name, set share
  connection parameter members
*/
int get_connection(MEM_ROOT *mem_root, FEDERATEDX_SHARE *share)
{
  int error_num= ER_FOREIGN_SERVER_DOESNT_EXIST;
  FOREIGN_SERVER *server, server_buffer;
  DBUG_ENTER("ha_federatedx::get_connection");

  /*
    get_server_by_name() clones the server if exists and allocates
	copies of strings in the supplied mem_root
  */
  if (!(server=
       get_server_by_name(mem_root, share->connection_string, &server_buffer)))
  {
    DBUG_PRINT("info", ("get_server_by_name returned > 0 error condition!"));
    /* need to come up with error handling */
    error_num=1;
    goto error;
  }
  DBUG_PRINT("info", ("get_server_by_name returned server at %p",
                      server));

  /*
    Most of these should never be empty strings, error handling will
    need to be implemented. Also, is this the best way to set the share
    members? Is there some allocation needed? In running this code, it works
    except there are errors in the trace file of the share being overrun 
    at the address of the share.
  */
  share->server_name_length= server->server_name_length;
  share->server_name= const_cast<char*>(server->server_name);
  share->username= const_cast<char*>(server->username);
  share->password= const_cast<char*>(server->password);
  share->database= const_cast<char*>(server->db);
  share->port= server->port > MIN_PORT && server->port < 65536 ? 
               (ushort) server->port : MYSQL_PORT;
  share->hostname= const_cast<char*>(server->host);
  if (!(share->socket= const_cast<char*>(server->socket)) &&
      !strcmp(share->hostname, my_localhost))
    share->socket= (char *) MYSQL_UNIX_ADDR;
  share->scheme= const_cast<char*>(server->scheme);

  DBUG_PRINT("info", ("share->username: %s", share->username));
  DBUG_PRINT("info", ("share->password: %s", share->password));
  DBUG_PRINT("info", ("share->hostname: %s", share->hostname));
  DBUG_PRINT("info", ("share->database: %s", share->database));
  DBUG_PRINT("info", ("share->port:     %d", share->port));
  DBUG_PRINT("info", ("share->socket:   %s", share->socket));
  DBUG_RETURN(0);

error:
  my_printf_error(error_num, "server name: '%s' doesn't exist!",
                  MYF(0), share->connection_string);
  DBUG_RETURN(error_num);
}

/*
  Parse connection info from table->s->connect_string

  SYNOPSIS
    parse_url()
    mem_root            MEM_ROOT pointer for memory allocation
    share               pointer to FEDERATEDX share
    table               pointer to current TABLE class
    table_create_flag   determines what error to throw

  DESCRIPTION
    Populates the share with information about the connection
    to the foreign database that will serve as the data source.
    This string must be specified (currently) in the "CONNECTION" field,
    listed in the CREATE TABLE statement.

    This string MUST be in the format of any of these:

    CONNECTION="scheme://username:password@hostname:port/database/table"
    CONNECTION="scheme://username@hostname/database/table"
    CONNECTION="scheme://username@hostname:port/database/table"
    CONNECTION="scheme://username:password@hostname/database/table"

    _OR_

    CONNECTION="connection name"

    

  An Example:

  CREATE TABLE t1 (id int(32))
    ENGINE="FEDERATEDX"
    CONNECTION="mysql://joe:joespass@192.168.1.111:9308/federatedx/testtable";

  CREATE TABLE t2 (
    id int(4) NOT NULL auto_increment,
    name varchar(32) NOT NULL,
    PRIMARY KEY(id)
    ) ENGINE="FEDERATEDX" CONNECTION="my_conn";

  ***IMPORTANT***
  Currently, the FederatedX Storage Engine only supports connecting to another
  Database ("scheme" of "mysql"). Connections using JDBC as well as 
  other connectors are in the planning stage.
  

  'password' and 'port' are both optional.

  RETURN VALUE
    0           success
    error_num   particular error code 

*/

static int parse_url(MEM_ROOT *mem_root, FEDERATEDX_SHARE *share,
                     TABLE_SHARE *table_s, uint table_create_flag)
{
  uint error_num= (table_create_flag ?
                   ER_FOREIGN_DATA_STRING_INVALID_CANT_CREATE :
                   ER_FOREIGN_DATA_STRING_INVALID);
  DBUG_ENTER("ha_federatedx::parse_url");

  share->port= 0;
  share->socket= 0;
  DBUG_PRINT("info", ("share at %p", share));
  DBUG_PRINT("info", ("Length: %u", (uint) table_s->connect_string.length));
  DBUG_PRINT("info", ("String: '%.*s'", (int) table_s->connect_string.length,
                      table_s->connect_string.str));
  share->connection_string= strmake_root(mem_root, table_s->connect_string.str,
                                       table_s->connect_string.length);

  DBUG_PRINT("info",("parse_url alloced share->connection_string %p",
                     share->connection_string));

  DBUG_PRINT("info",("share->connection_string: %s",share->connection_string));
  /*
    No :// or @ in connection string. Must be a straight connection name of
    either "servername" or "servername/tablename"
  */
  if ((!strstr(share->connection_string, "://") &&
       (!strchr(share->connection_string, '@'))))
  {

    DBUG_PRINT("info",
               ("share->connection_string: %s  internal format "
                "share->connection_string: %p",
                share->connection_string,
                share->connection_string));

    /* ok, so we do a little parsing, but not completely! */
    share->parsed= FALSE;
    /*
      If there is a single '/' in the connection string, this means the user is
      specifying a table name
    */

    if ((share->table_name= strchr(share->connection_string, '/')))
    {
      *share->table_name++= '\0';
      share->table_name_length= strlen(share->table_name);

      DBUG_PRINT("info", 
                 ("internal format, parsed table_name "
                  "share->connection_string: %s  share->table_name: %s",
                  share->connection_string, share->table_name));

      /*
        there better not be any more '/'s !
      */
      if (strchr(share->table_name, '/'))
        goto error;
    }
    /*
      Otherwise, straight server name, use tablename of federatedx table
      as remote table name
    */
    else
    {
      /*
        Connection specifies everything but, resort to
        expecting remote and foreign table names to match
      */
      share->table_name= strmake_root(mem_root, table_s->table_name.str,
                                      (share->table_name_length=
                                       table_s->table_name.length));
      DBUG_PRINT("info", 
                 ("internal format, default table_name "
                  "share->connection_string: %s  share->table_name: %s",
                  share->connection_string, share->table_name));
    }

    if ((error_num= get_connection(mem_root, share)))
      goto error;
  }
  else
  {
    share->parsed= TRUE;
    // Add a null for later termination of table name
    share->connection_string[table_s->connect_string.length]= 0;
    share->scheme= share->connection_string;
    DBUG_PRINT("info",("parse_url alloced share->scheme: %p",
                       share->scheme));

    /*
      Remove addition of null terminator and store length
      for each string  in share
    */
    if (!(share->username= strstr(share->scheme, "://")))
      goto error;
    share->scheme[share->username - share->scheme]= '\0';

    if (!federatedx_io::handles_scheme(share->scheme))
      goto error;

    share->username+= 3;

    if (!(share->hostname= strchr(share->username, '@')))
      goto error;
    *share->hostname++= '\0';                   // End username

    if ((share->password= strchr(share->username, ':')))
    {
      *share->password++= '\0';                 // End username

      /* make sure there isn't an extra / or @ */
      if ((strchr(share->password, '/') || strchr(share->hostname, '@')))
        goto error;
      /*
        Found that if the string is:
        user:@hostname:port/db/table
        Then password is a null string, so set to NULL
      */
      if (share->password[0] == '\0')
        share->password= NULL;
    }

    /* make sure there isn't an extra / or @ */
    if ((strchr(share->username, '/')) || (strchr(share->hostname, '@')))
      goto error;

    if (!(share->database= strchr(share->hostname, '/')))
      goto error;
    *share->database++= '\0';

    if ((share->sport= strchr(share->hostname, ':')))
    {
      *share->sport++= '\0';
      if (share->sport[0] == '\0')
        share->sport= NULL;
      else
        share->port= atoi(share->sport);
    }

    if (!(share->table_name= strchr(share->database, '/')))
      goto error;
    *share->table_name++= '\0';

    share->table_name_length= strlen(share->table_name);

    /* make sure there's not an extra / */
    if ((strchr(share->table_name, '/')))
      goto error;

    if (share->hostname[0] == '\0')
      share->hostname= NULL;

  }
  if (!share->port)
  {
    if (!share->hostname || strcmp(share->hostname, my_localhost) == 0)
      share->socket= (char *) MYSQL_UNIX_ADDR;
    else
      share->port= MYSQL_PORT;
  }

  DBUG_PRINT("info",
             ("scheme: %s  username: %s  password: %s  hostname: %s  "
              "port: %d  db: %s  tablename: %s",
              share->scheme, share->username, share->password,
              share->hostname, share->port, share->database,
              share->table_name));

  DBUG_RETURN(0);

error:
  DBUG_RETURN(parse_url_error(share, table_s, error_num));
}

/*****************************************************************************
** FEDERATEDX tables
*****************************************************************************/

ha_federatedx::ha_federatedx(handlerton *hton,
                           TABLE_SHARE *table_arg)
  :handler(hton, table_arg),
   txn(0), io(0), stored_result(0)
{
  bzero(&bulk_insert, sizeof(bulk_insert));
  bulk_insert_size = 0;
  bzero(&additionalFilter, sizeof(additionalFilter));
  init_dynamic_string(&additionalFilter, NULL, FEDERATEDX_QUERY_BUFFER_SIZE, FEDERATEDX_QUERY_BUFFER_SIZE);
  bzero(&pr_info.partial_read_query, sizeof(pr_info.partial_read_query));
  init_dynamic_string(&pr_info.partial_read_query, NULL, FEDERATEDX_QUERY_BUFFER_SIZE, FEDERATEDX_QUERY_BUFFER_SIZE);
  bzero(&pr_info.partial_read_filter, sizeof(pr_info.partial_read_filter));
  init_dynamic_string(&pr_info.partial_read_filter, NULL, FEDERATEDX_QUERY_BUFFER_SIZE, FEDERATEDX_QUERY_BUFFER_SIZE);
  part_col = NULL;
  local_part_col_name = NULL;
  local_shard_info_result = NULL;
  max_query_size = 0;
  index_cardinality_init = false;
  records_per_shard = 0;
  partial_ppd = TRUE;
  bzero(index_cardinality, sizeof(index_cardinality));
  vindex_init = false;
  bzero(&vindex_set, sizeof(vindex_set));
  pk_num = -1;
  bzero(&pk_set, sizeof(pk_set));
  field_type = FIELD_TYPE_UNKNOWN;
  table_status_init = false;
  table_status_init_time = 0;
  records_at_init_time = 2;
  insert_records_since_init = 0;
  delete_records_since_init = 0;
  update_records_since_init = 0;
}


/*
  Convert MySQL result set row to handler internal format

  SYNOPSIS
    convert_row_to_internal_format()
      record    Byte pointer to record
      row       MySQL result set row from fetchrow()
      result	Result set to use

  DESCRIPTION
    This method simply iterates through a row returned via fetchrow with
    values from a successful SELECT , and then stores each column's value
    in the field object via the field object pointer (pointing to the table's
    array of field object pointers). This is how the handler needs the data
    to be stored to then return results back to the user

  RETURN VALUE
    0   After fields have had field values stored from record
*/

uint ha_federatedx::convert_row_to_internal_format(uchar *record,
                                                  FEDERATEDX_IO_ROW *row,
                                                  FEDERATEDX_IO_RESULT *result)
{
  ulong *lengths;
  Field **field;
  int column= 0;
  my_bitmap_map *old_map= dbug_tmp_use_all_columns(table, table->write_set);
  Time_zone *saved_time_zone= table->in_use->variables.time_zone;
  DBUG_ENTER("ha_federatedx::convert_row_to_internal_format");

  table->in_use->variables.time_zone= UTC;
  lengths= io->fetch_lengths(result);

  for (field= table->field; *field; field++, column++)
  {
    /*
      index variable to move us through the row at the
      same iterative step as the field
    */
    my_ptrdiff_t old_ptr;
    old_ptr= (my_ptrdiff_t) (record - table->record[0]);
    (*field)->move_field_offset(old_ptr);
    if (io->is_column_null(row, column))
      (*field)->set_null();
    else
    {
      if (bitmap_is_set(table->read_set, (*field)->field_index))
      {
        (*field)->set_notnull();
        if ((*field)->flags & BLOB_FLAG) {
          ((Field_blob*)(*field))->swap_value_and_read_value();
        }
        (*field)->store(io->get_column_data(row, column), lengths[column], &my_charset_bin);
        if ((*field)->flags & BLOB_FLAG) {
          ((Field_blob*)(*field))->swap_value_and_read_value();
        }
      }
    }
    (*field)->move_field_offset(-old_ptr);
  }
  table->in_use->variables.time_zone= saved_time_zone;
  dbug_tmp_restore_column_map(table->write_set, old_map);
  DBUG_RETURN(0);
}

static bool emit_key_part_name(String *to, KEY_PART_INFO *part)
{
  DBUG_ENTER("emit_key_part_name");
  if (append_ident(to, part->field->field_name.str,
                   part->field->field_name.length, ident_quote_char))
    DBUG_RETURN(1);                           // Out of memory
  DBUG_RETURN(0);
}

static bool emit_key_part_element(String *to, KEY_PART_INFO *part,
                                  bool needs_quotes, bool is_like,
                                  const uchar *ptr, uint len)
{
  Field *field= part->field;
  DBUG_ENTER("emit_key_part_element");

  if (needs_quotes && to->append(STRING_WITH_LEN("'")))
    DBUG_RETURN(1);

  if (part->type == HA_KEYTYPE_BIT)
  {
    char buff[STRING_BUFFER_USUAL_SIZE], *buf= buff;

    *buf++= '0';
    *buf++= 'x';
    buf= octet2hex(buf, (char*) ptr, len);
    if (to->append((char*) buff, (uint)(buf - buff)))
      DBUG_RETURN(1);
  }
  else if (part->key_part_flag & HA_BLOB_PART)
  {
    String blob;
    uint blob_length= uint2korr(ptr);
    blob.set_quick((char*) ptr+HA_KEY_BLOB_LENGTH,
                   blob_length, &my_charset_bin);
    if (to->append_for_single_quote(&blob))
      DBUG_RETURN(1);
  }
  else if (part->key_part_flag & HA_VAR_LENGTH_PART)
  {
    String varchar;
    uint var_length= uint2korr(ptr);
    varchar.set_quick((char*) ptr+HA_KEY_BLOB_LENGTH,
                      var_length, &my_charset_bin);
    if (to->append_for_single_quote(&varchar))
      DBUG_RETURN(1);
  }
  else
  {
    char strbuff[MAX_FIELD_WIDTH];
    String str(strbuff, sizeof(strbuff), part->field->charset()), *res;

    res= field->val_str(&str, ptr);

    if (field->result_type() == STRING_RESULT)
    {
      if (to->append_for_single_quote(res))
        DBUG_RETURN(1);
    }
    else if (to->append(res->ptr(), res->length()))
      DBUG_RETURN(1);
  }

  if (is_like && to->append(STRING_WITH_LEN("%")))
    DBUG_RETURN(1);

  if (needs_quotes && to->append(STRING_WITH_LEN("'")))
    DBUG_RETURN(1);

  DBUG_RETURN(0);
}

/*
  Create a WHERE clause based off of values in keys
  Note: This code was inspired by key_copy from key.cc

  SYNOPSIS
    create_where_from_key ()
      to          String object to store WHERE clause
      key_info    KEY struct pointer
      key         byte pointer containing key
      key_length  length of key
      range_type  0 - no range, 1 - min range, 2 - max range
                  (see enum range_operation)

  DESCRIPTION
    Using iteration through all the keys via a KEY_PART_INFO pointer,
    This method 'extracts' the value of each key in the byte pointer
    *key, and for each key found, constructs an appropriate WHERE clause

  RETURN VALUE
    0   After all keys have been accounted for to create the WHERE clause
    1   No keys found

    Range flags Table per Timour:

   -----------------
   - start_key:
     * ">"  -> HA_READ_AFTER_KEY
     * ">=" -> HA_READ_KEY_OR_NEXT
     * "="  -> HA_READ_KEY_EXACT

   - end_key:
     * "<"  -> HA_READ_BEFORE_KEY
     * "<=" -> HA_READ_AFTER_KEY

   records_in_range:
   -----------------
   - start_key:
     * ">"  -> HA_READ_AFTER_KEY
     * ">=" -> HA_READ_KEY_EXACT
     * "="  -> HA_READ_KEY_EXACT

   - end_key:
     * "<"  -> HA_READ_BEFORE_KEY
     * "<=" -> HA_READ_AFTER_KEY
     * "="  -> HA_READ_AFTER_KEY

0 HA_READ_KEY_EXACT,              Find first record else error
1 HA_READ_KEY_OR_NEXT,            Record or next record
2 HA_READ_KEY_OR_PREV,            Record or previous
3 HA_READ_AFTER_KEY,              Find next rec. after key-record
4 HA_READ_BEFORE_KEY,             Find next rec. before key-record
5 HA_READ_PREFIX,                 Key which as same prefix
6 HA_READ_PREFIX_LAST,            Last key with the same prefix
7 HA_READ_PREFIX_LAST_OR_PREV,    Last or prev key with the same prefix

Flags that I've found:

id, primary key, varchar

id = 'ccccc'
records_in_range: start_key 0 end_key 3
read_range_first: start_key 0 end_key NULL

id > 'ccccc'
records_in_range: start_key 3 end_key NULL
read_range_first: start_key 3 end_key NULL

id < 'ccccc'
records_in_range: start_key NULL end_key 4
read_range_first: start_key NULL end_key 4

id <= 'ccccc'
records_in_range: start_key NULL end_key 3
read_range_first: start_key NULL end_key 3

id >= 'ccccc'
records_in_range: start_key 0 end_key NULL
read_range_first: start_key 1 end_key NULL

id like 'cc%cc'
records_in_range: start_key 0 end_key 3
read_range_first: start_key 1 end_key 3

id > 'aaaaa' and id < 'ccccc'
records_in_range: start_key 3 end_key 4
read_range_first: start_key 3 end_key 4

id >= 'aaaaa' and id < 'ccccc';
records_in_range: start_key 0 end_key 4
read_range_first: start_key 1 end_key 4

id >= 'aaaaa' and id <= 'ccccc';
records_in_range: start_key 0 end_key 3
read_range_first: start_key 1 end_key 3

id > 'aaaaa' and id <= 'ccccc';
records_in_range: start_key 3 end_key 3
read_range_first: start_key 3 end_key 3

numeric keys:

id = 4
index_read_idx: start_key 0 end_key NULL 

id > 4
records_in_range: start_key 3 end_key NULL
read_range_first: start_key 3 end_key NULL

id >= 4
records_in_range: start_key 0 end_key NULL
read_range_first: start_key 1 end_key NULL

id < 4
records_in_range: start_key NULL end_key 4
read_range_first: start_key NULL end_key 4

id <= 4
records_in_range: start_key NULL end_key 3
read_range_first: start_key NULL end_key 3

id like 4
full table scan, select * from

id > 2 and id < 8
records_in_range: start_key 3 end_key 4
read_range_first: start_key 3 end_key 4

id >= 2 and id < 8
records_in_range: start_key 0 end_key 4
read_range_first: start_key 1 end_key 4

id >= 2 and id <= 8
records_in_range: start_key 0 end_key 3
read_range_first: start_key 1 end_key 3

id > 2 and id <= 8
records_in_range: start_key 3 end_key 3
read_range_first: start_key 3 end_key 3

multi keys (id int, name varchar, other varchar)

id = 1;
records_in_range: start_key 0 end_key 3
read_range_first: start_key 0 end_key NULL

id > 4;
id > 2 and name = '333'; remote: id > 2
id > 2 and name > '333'; remote: id > 2
id > 2 and name > '333' and other < 'ddd'; remote: id > 2 no results
id > 2 and name >= '333' and other < 'ddd'; remote: id > 2 1 result
id >= 4 and name = 'eric was here' and other > 'eeee';
records_in_range: start_key 3 end_key NULL
read_range_first: start_key 3 end_key NULL

id >= 4;
id >= 2 and name = '333' and other < 'ddd';
remote: `id`  >= 2 AND `name`  >= '333';
records_in_range: start_key 0 end_key NULL
read_range_first: start_key 1 end_key NULL

id < 4;
id < 3 and name = '222' and other <= 'ccc'; remote: id < 3
records_in_range: start_key NULL end_key 4
read_range_first: start_key NULL end_key 4

id <= 4;
records_in_range: start_key NULL end_key 3
read_range_first: start_key NULL end_key 3

id like 4;
full table scan

id  > 2 and id < 4;
records_in_range: start_key 3 end_key 4
read_range_first: start_key 3 end_key 4

id >= 2 and id < 4;
records_in_range: start_key 0 end_key 4
read_range_first: start_key 1 end_key 4

id >= 2 and id <= 4;
records_in_range: start_key 0 end_key 3
read_range_first: start_key 1 end_key 3

id > 2 and id <= 4;
id = 6 and name = 'eric was here' and other > 'eeee';
remote: (`id`  > 6 AND `name`  > 'eric was here' AND `other`  > 'eeee')
AND (`id`  <= 6) AND ( AND `name`  <= 'eric was here')
no results
records_in_range: start_key 3 end_key 3
read_range_first: start_key 3 end_key 3

Summary:

* If the start key flag is 0 the max key flag shouldn't even be set, 
  and if it is, the query produced would be invalid.
* Multipart keys, even if containing some or all numeric columns,
  are treated the same as non-numeric keys

  If the query is " = " (quotes or not):
  - records in range start key flag HA_READ_KEY_EXACT,
    end key flag HA_READ_AFTER_KEY (incorrect)
  - any other: start key flag HA_READ_KEY_OR_NEXT,
    end key flag HA_READ_AFTER_KEY (correct)

* 'like' queries (of key)
  - Numeric, full table scan
  - Non-numeric
      records_in_range: start_key 0 end_key 3
      other : start_key 1 end_key 3

* If the key flag is HA_READ_AFTER_KEY:
   if start_key, append >
   if end_key, append <=

* If create_where_key was called by records_in_range:

 - if the key is numeric:
    start key flag is 0 when end key is NULL, end key flag is 3 or 4
 - if create_where_key was called by any other function:
    start key flag is 1 when end key is NULL, end key flag is 3 or 4
 - if the key is non-numeric, or multipart
    When the query is an exact match, the start key flag is 0,
    end key flag is 3 for what should be a no-range condition where
    you should have 0 and max key NULL, which it is if called by
    read_range_first

Conclusion:

1. Need logic to determin if a key is min or max when the flag is
HA_READ_AFTER_KEY, and handle appending correct operator accordingly

2. Need a boolean flag to pass to create_where_from_key, used in the
switch statement. Add 1 to the flag if:
  - start key flag is HA_READ_KEY_EXACT and the end key is NULL

*/

bool ha_federatedx::create_where_from_key(String *to,
                                         KEY *key_info,
                                         const key_range *start_key,
                                         const key_range *end_key,
                                         bool from_records_in_range,
                                         bool eq_range)
{
  bool both_not_null=
    (start_key != NULL && end_key != NULL) ? TRUE : FALSE;
  const uchar *ptr;
  uint remainder, length;
  char tmpbuff[FEDERATEDX_QUERY_BUFFER_SIZE];
  String tmp(tmpbuff, sizeof(tmpbuff), system_charset_info);
  const key_range *ranges[2]= { start_key, end_key };
  Time_zone *saved_time_zone= table->in_use->variables.time_zone;
  my_bitmap_map *old_map;
  DBUG_ENTER("ha_federatedx::create_where_from_key");

  tmp.length(0); 
  if (start_key == NULL && end_key == NULL)
    DBUG_RETURN(1);

  table->in_use->variables.time_zone= UTC;
  old_map= dbug_tmp_use_all_columns(table, table->write_set);
  for (uint i= 0; i <= 1; i++)
  {
    bool needs_quotes;
    KEY_PART_INFO *key_part;
    if (ranges[i] == NULL)
      continue;

    if (both_not_null)
    {
      if (i > 0)
        tmp.append(STRING_WITH_LEN(") AND ("));
      else
        tmp.append(STRING_WITH_LEN(" ("));
    }

    for (key_part=  key_info->key_part,
           remainder= key_info->user_defined_key_parts,
           length= ranges[i]->length,
           ptr= ranges[i]->key; ;
         remainder--,
           key_part++)
    {
      Field *field= key_part->field;
      uint store_length= key_part->store_length;
      uint part_length= MY_MIN(store_length, length);
      needs_quotes= field->str_needs_quotes();
      DBUG_DUMP("key, start of loop", ptr, length);

      if (key_part->null_bit)
      {
        if (*ptr++)
        {
          /*
            We got "IS [NOT] NULL" condition against nullable column. We
            distinguish between "IS NOT NULL" and "IS NULL" by flag. For
            "IS NULL", flag is set to HA_READ_KEY_EXACT.
          */
          if (emit_key_part_name(&tmp, key_part) ||
              tmp.append(ranges[i]->flag == HA_READ_KEY_EXACT ?
                         " IS NULL " : " IS NOT NULL "))
            goto err;
          /*
            We need to adjust pointer and length to be prepared for next
            key part. As well as check if this was last key part.
          */
          goto prepare_for_next_key_part;
        }
      }

      if (tmp.append(STRING_WITH_LEN(" (")))
        goto err;

      switch (ranges[i]->flag) {
      case HA_READ_KEY_EXACT:
        DBUG_PRINT("info", ("federatedx HA_READ_KEY_EXACT %d", i));
        if (store_length >= length ||
            !needs_quotes ||
            key_part->type == HA_KEYTYPE_BIT ||
            field->result_type() != STRING_RESULT)
        {
          if (emit_key_part_name(&tmp, key_part))
            goto err;

          if (from_records_in_range)
          {
            if (tmp.append(STRING_WITH_LEN(" >= ")))
              goto err;
          }
          else
          {
            if (tmp.append(STRING_WITH_LEN(" = ")))
              goto err;
          }

          if (emit_key_part_element(&tmp, key_part, needs_quotes, 0, ptr,
                                    part_length))
            goto err;
        }
        else
        {
          /* LIKE */
          if (emit_key_part_name(&tmp, key_part) ||
              tmp.append(STRING_WITH_LEN(" LIKE ")) ||
              emit_key_part_element(&tmp, key_part, needs_quotes, 1, ptr,
                                    part_length))
            goto err;
        }
        break;
      case HA_READ_AFTER_KEY:
        if (eq_range)
        {
          if (tmp.append("1=1"))                // Dummy
            goto err;
          break;
        }
        DBUG_PRINT("info", ("federatedx HA_READ_AFTER_KEY %d", i));
        if (store_length >= length || i > 0) /* end key */
        {
          if (emit_key_part_name(&tmp, key_part))
            goto err;

          if (i > 0) /* end key */
          {
            if (tmp.append(STRING_WITH_LEN(" <= ")))
              goto err;
          }
          else /* start key */
          {
            if (tmp.append(STRING_WITH_LEN(" > ")))
              goto err;
          }

          if (emit_key_part_element(&tmp, key_part, needs_quotes, 0, ptr,
                                    part_length))
          {
            goto err;
          }
          break;
        }
        /* fall through */
      case HA_READ_KEY_OR_NEXT:
        DBUG_PRINT("info", ("federatedx HA_READ_KEY_OR_NEXT %d", i));
        if (emit_key_part_name(&tmp, key_part) ||
            tmp.append(STRING_WITH_LEN(" >= ")) ||
            emit_key_part_element(&tmp, key_part, needs_quotes, 0, ptr,
              part_length))
          goto err;
        break;
      case HA_READ_BEFORE_KEY:
        DBUG_PRINT("info", ("federatedx HA_READ_BEFORE_KEY %d", i));
        if (store_length >= length)
        {
          if (emit_key_part_name(&tmp, key_part) ||
              tmp.append(STRING_WITH_LEN(" < ")) ||
              emit_key_part_element(&tmp, key_part, needs_quotes, 0, ptr,
                                    part_length))
            goto err;
          break;
        }
        /* fall through */
      case HA_READ_KEY_OR_PREV:
        DBUG_PRINT("info", ("federatedx HA_READ_KEY_OR_PREV %d", i));
        if (emit_key_part_name(&tmp, key_part) ||
            tmp.append(STRING_WITH_LEN(" <= ")) ||
            emit_key_part_element(&tmp, key_part, needs_quotes, 0, ptr,
                                  part_length))
          goto err;
        break;
      default:
        DBUG_PRINT("info",("cannot handle flag %d", ranges[i]->flag));
        goto err;
      }
      if (tmp.append(STRING_WITH_LEN(") ")))
        goto err;

prepare_for_next_key_part:
      if (store_length >= length)
        break;
      DBUG_PRINT("info", ("remainder %d", remainder));
      DBUG_ASSERT(remainder > 1);
      length-= store_length;
      /*
        For nullable columns, null-byte is already skipped before, that is
        ptr was incremented by 1. Since store_length still counts null-byte,
        we need to subtract 1 from store_length.
      */
      ptr+= store_length - MY_TEST(key_part->null_bit);
      if (tmp.append(STRING_WITH_LEN(" AND ")))
        goto err;

      DBUG_PRINT("info",
                 ("create_where_from_key WHERE clause: %s",
                  tmp.c_ptr_quick()));
    }
  }
  dbug_tmp_restore_column_map(table->write_set, old_map);
  table->in_use->variables.time_zone= saved_time_zone;

  if (both_not_null)
    if (tmp.append(STRING_WITH_LEN(") ")))
      DBUG_RETURN(1);

  if (to->append(STRING_WITH_LEN(" WHERE ")))
    DBUG_RETURN(1);

  if (to->append(tmp))
    DBUG_RETURN(1);
  if (additionalFilter.length != 0) {
    if (to->append(STRING_WITH_LEN(" AND ("))) {
        dynstr_trunc(&additionalFilter, additionalFilter.length);
      DBUG_RETURN(1);
    }
    if (to->append(additionalFilter.str, additionalFilter.length)) {
      dynstr_trunc(&additionalFilter, additionalFilter.length);
      DBUG_RETURN(1);
    }
    if (to->append(STRING_WITH_LEN(")"))) {
      dynstr_trunc(&additionalFilter, additionalFilter.length);
      DBUG_RETURN(1);
    }
  }


  DBUG_RETURN(0);

err:
  dbug_tmp_restore_column_map(table->write_set, old_map);
  table->in_use->variables.time_zone= saved_time_zone;
  DBUG_RETURN(1);
}

/**
 * @param scan_mode
 * scan mode is the scan type for vitess remote database, when reading
 * from vitess, generally speaking, there are two category of the scan type:
 * a. which workload type the vitess server is when reading the data:
 *    1. OLTP, it means batch read && transaction aware read
 *    2. OLAP, it means stream read, but it does not support transaction.
 *       Even the mode is OLAP, the server will collect all the results
 *       and stored it in its memory, so OLAP mode only means streaming
 *       at vitess's side, not server side
 * b. how the server doing a complete table scan for vitess table
 *    1. server read all data from remote vitess database through one query
 *    2. server read all data from remote vitess database through several
 *       queries(known as partial read or incremental read). Partial read
 *       is extremely useful when the user query has a limit clause. For
 *       partial read, we need to divide vitess's table into several parts,
 *       and each query read a part of the data. There are two strategies to
 *       divide the table:
 *       i)  based on vitess's shard, this is called shard read
 *       ii) based on range, this is called range read
 * information needed by shard read/range read is provided by vitess.
 *
 * scan mode can be specified by sql hint `fetch mode 'value'` explicitly in user's
 * query, for example, `select * from t fetch mode 'olap'` specified the scan
 * mode for table t is OLAP.
 *
 * the valid fetch mode string and its meaning is listed as below
 *
 * 'olap'        -> scan_mode = olap,    partial_read_mode = default
 * 'oltp'        -> scan_mode = oltp,    partial_read_mode = default
 * 'sd_rd'       -> scan_mode = default, partial_read_mode = shard read
 * 'rg_rd'       -> scan_mode = default, partial_read_mode = range read
 * 'full_rd'     -> scan_mode = default, partial_read_mode = none
 * 'rg_sd_rd'    -> scan_mode = default, partial_read_mode = range shard read
 * 'sd_tp_rd'    -> scan_mode = oltp,    partial_read_mode = shard read
 * 'sd_ap_rd'    -> scan_mode = olap,    partial_read_mode = shard read
 * 'rg_tp_rd'    -> scan_mode = oltp,    partial_read_mode = range read
 * 'rg_ap_rd'    -> scan_mode = olap,    partial_read_mode = range read
 * 'full_ap_rd'  -> scan_mode = olap,    partial_read_mode = none
 * 'full_tp_rd'  -> scan_mode = oltp,    partial_read_mode = none
 * 'rg_sd_ap_rd' -> scan_mode = oltp,    partial_read_mode = range shard read
 * 'rg_sd_tp_rd' -> scan_mode = olap,    partial_read_mode = range shard read
 *
 */
void ha_federatedx::set_scan_mode(LEX_CSTRING scan_mode) {
    if (!scan_mode.str) {
      return;
    }

    if (scan_mode.length == 4) {
      if (!strcasecmp(scan_mode.str, "oltp")) {
        this->scan_mode = SCAN_MODE_OLTP;
      } else if (!strcasecmp(scan_mode.str, "olap")) {
        this->scan_mode = SCAN_MODE_OLAP;
      }
      return;
    } else if (scan_mode.length == 5) {
      if (!strcasecmp(scan_mode.str, "sd_rd")) {
        this->partial_read_mode_by_hint = PARTIAL_READ_SHARD_READ;
      } else if (!strcasecmp(scan_mode.str, "rg_rd")) {
        this->partial_read_mode_by_hint = PARTIAL_READ_RANGE_READ;
      }
      return;
    } else if (scan_mode.length == 7) {
      if (!strcasecmp(scan_mode.str, "full_rd")) {
        this->partial_read_mode_by_hint = PARTIAL_READ_NONE;
      }
      return;
    } else if (scan_mode.length == 8) {
      if (!strcasecmp(scan_mode.str, "sd_tp_rd")) {
        this->scan_mode = SCAN_MODE_OLTP;
        this->partial_read_mode_by_hint = PARTIAL_READ_SHARD_READ;
      } else if (!strcasecmp(scan_mode.str, "sd_ap_rd")) {
        this->scan_mode = SCAN_MODE_OLAP;
        this->partial_read_mode_by_hint = PARTIAL_READ_SHARD_READ;
      } else if (!strcasecmp(scan_mode.str, "rg_tp_rd")) {
        this->scan_mode = SCAN_MODE_OLTP;
        this->partial_read_mode_by_hint = PARTIAL_READ_RANGE_READ;
      } else if (!strcasecmp(scan_mode.str, "rg_ap_rd")) {
        this->scan_mode = SCAN_MODE_OLAP;
        this->partial_read_mode_by_hint = PARTIAL_READ_RANGE_READ;
      } else if (!strcasecmp(scan_mode.str, "rg_sd_rd")) {
        this->partial_read_mode_by_hint = PARTIAL_READ_SHARD_RANGE_READ;
      }
      return;
    } else if (scan_mode.length == 10) {
      if (!strcasecmp(scan_mode.str, "full_ap_rd")) {
        this->scan_mode = SCAN_MODE_OLAP;
        this->partial_read_mode_by_hint = PARTIAL_READ_NONE;
      } else if (!strcasecmp(scan_mode.str, "full_tp_rd")) {
        this->scan_mode = SCAN_MODE_OLTP;
        this->partial_read_mode_by_hint = PARTIAL_READ_NONE;
      }
      return;
    } else if (scan_mode.length == 11) {
      if (!strcasecmp(scan_mode.str, "rg_sd_tp_rd")) {
        this->scan_mode = SCAN_MODE_OLTP;
        this->partial_read_mode_by_hint = PARTIAL_READ_SHARD_RANGE_READ;
      } else if (!strcasecmp(scan_mode.str, "rg_sd_ap_rd")) {
        this->scan_mode = SCAN_MODE_OLAP;
        this->partial_read_mode_by_hint = PARTIAL_READ_SHARD_RANGE_READ;
      }
      return;
    }
}

const COND *ha_federatedx::cond_push(const Item *cond) {
  // convert cond to string
  DBUG_ENTER("ha_federated::cond_push");

#if 1
  if ((cond->used_tables() & ~table->pos_in_table_list->get_map()))
  {
    /**
     * 'cond' refers fields from other tables, or other instances
     * of this table, -> reject it.
     * (Optimizer need to have a better understanding of what is
     *  pushable by each handler.)
     */
//    DBUG_EXECUTE("where",print_where((COND *)cond, "Rejected cond_push", QT_ORDINARY););
    DBUG_RETURN(cond);
  }
#else
  /*
    Make sure that 'cond' does not refer field(s) from other tables
    or other instances of this table.
    (This was a legacy bug in optimizer)
  */
  DBUG_ASSERT(!(cond->used_tables() & ~table->pos_in_table_list->map()));
#endif
  if (additionalFilter.length == 0) {
    char buff[1024];
    String *res;
    String filter(buff, sizeof(buff), system_charset_info);
    filter.length(0);
    cond->walk_const(&Item::has_equal_condition, false, &has_equal_filter);
    res = cond->to_str(&filter, ha_thd());
    if (res != 0 && res->length() > 0) {
        dynstr_append_mem(&additionalFilter, res->ptr(), res->length());
    }
    //filter.length(0);
    //((Item *)cond)->print(&filter, QT_ORDINARY);
    //filter.length();
    if (additionalFilter.length != 0) {
      partial_ppd = FALSE;
      DBUG_RETURN(0);
    }
    filter.length(0);
    res = cond->partial_to_str(&filter, ha_thd());
    if (res != 0 && res->length() > 0) {
      dynstr_append_mem(&additionalFilter, res->ptr(), res->length());
    }
  }
  DBUG_RETURN(cond);
}

const DYNAMIC_STRING *ha_federatedx::ha_pushed_condition() const { return &additionalFilter;}


static void fill_server(MEM_ROOT *mem_root, FEDERATEDX_SERVER *server,
                        FEDERATEDX_SHARE *share, CHARSET_INFO *table_charset)
{
  char buffer[STRING_BUFFER_USUAL_SIZE];
  String key(buffer, sizeof(buffer), &my_charset_bin);  
  String scheme(share->scheme, &my_charset_latin1);
  String hostname(share->hostname, &my_charset_latin1);
  String database(share->database, system_charset_info);
  String username(share->username, system_charset_info);
  String socket(share->socket ? share->socket : "", files_charset_info);
  String password(share->password ? share->password : "", &my_charset_bin);
  DBUG_ENTER("fill_server");

  /* Do some case conversions */
  scheme.reserve(scheme.length());
  scheme.length(my_casedn_str(&my_charset_latin1, scheme.c_ptr_safe()));
  
  hostname.reserve(hostname.length());
  hostname.length(my_casedn_str(&my_charset_latin1, hostname.c_ptr_safe()));
  
  if (lower_case_table_names)
  {
    database.reserve(database.length());
    database.length(my_casedn_str(system_charset_info, database.c_ptr_safe()));
  }

#ifndef __WIN__
  /*
    TODO: there is no unix sockets under windows so the engine should be
    revised about using sockets in such environment.
  */
  if (lower_case_file_system && socket.length())
  {
    socket.reserve(socket.length());
    socket.length(my_casedn_str(files_charset_info, socket.c_ptr_safe()));
  }
#endif

  /* start with all bytes zeroed */  
  bzero(server, sizeof(*server));

  key.length(0);
  key.reserve(scheme.length() + hostname.length() + database.length() +
              socket.length() + username.length() + password.length() +
       sizeof(int) + 8);
  key.append(scheme);
  key.q_append('\0');
  server->hostname= (const char *) (intptr) key.length();
  key.append(hostname);
  key.q_append('\0');
  server->database= (const char *) (intptr) key.length();
  key.append(database);
  key.q_append('\0');
  key.q_append((uint32) share->port);
  server->socket= (const char *) (intptr) key.length();
  key.append(socket);
  key.q_append('\0');
  server->username= (const char *) (intptr) key.length();
  key.append(username);
  key.q_append('\0');
  server->password= (const char *) (intptr) key.length();
  key.append(password);
  key.c_ptr_safe();                             // Ensure we have end \0

  server->key_length= key.length();
  /* Copy and add end \0 */
  server->key= (uchar *)  strmake_root(mem_root, key.ptr(), key.length());

  /* pointer magic */
  server->scheme+= (intptr) server->key;
  server->hostname+= (intptr) server->key;
  server->database+= (intptr) server->key;
  server->username+= (intptr) server->key;
  server->password+= (intptr) server->key;
  server->socket+= (intptr) server->key;
  server->port= share->port;

  if (!share->socket)
    server->socket= NULL;
  if (!share->password)
    server->password= NULL;

  if (table_charset)
    server->csname= strdup_root(mem_root, table_charset->csname);

  DBUG_VOID_RETURN;
}


static FEDERATEDX_SERVER *get_server(FEDERATEDX_SHARE *share, TABLE *table)
{
  FEDERATEDX_SERVER *server= NULL, tmp_server;
  MEM_ROOT mem_root;
  char buffer[STRING_BUFFER_USUAL_SIZE];
  String key(buffer, sizeof(buffer), &my_charset_bin);  
  String scheme(share->scheme, &my_charset_latin1);
  String hostname(share->hostname, &my_charset_latin1);
  String database(share->database, system_charset_info);
  String username(share->username, system_charset_info);
  String socket(share->socket ? share->socket : "", files_charset_info);
  String password(share->password ? share->password : "", &my_charset_bin);
  DBUG_ENTER("ha_federated.cc::get_server");

  mysql_mutex_assert_owner(&federatedx_mutex);

  init_alloc_root(&mem_root, "federated", 4096, 4096, MYF(0));

  fill_server(&mem_root, &tmp_server, share, table ? table->s->table_charset : 0);

  if (!(server= (FEDERATEDX_SERVER *) my_hash_search(&federatedx_open_servers,
                                                 tmp_server.key,
                                                 tmp_server.key_length)))
  {
    if (!table || !tmp_server.csname)
      goto error;
 
    if (!(server= (FEDERATEDX_SERVER *) memdup_root(&mem_root, 
                          (char *) &tmp_server,
                          sizeof(*server))))
      goto error;

    server->mem_root= mem_root;

    if (my_hash_insert(&federatedx_open_servers, (uchar*) server))
      goto error;

    mysql_mutex_init(fe_key_mutex_FEDERATEDX_SERVER_mutex,
                     &server->mutex, MY_MUTEX_INIT_FAST);
  }
  else
    free_root(&mem_root, MYF(0)); /* prevents memory leak */

  server->use_count++;
  
  DBUG_RETURN(server);
error:
  free_root(&mem_root, MYF(0));
  DBUG_RETURN(NULL);
}


/*
  Example of simple lock controls. The "share" it creates is structure we will
  pass to each federatedx handler. Do you have to have one of these? Well, you
  have pieces that are used for locking, and they are needed to function.
*/

static FEDERATEDX_SHARE *get_share(const char *table_name, TABLE *table)
{
  char query_buffer[FEDERATEDX_QUERY_BUFFER_SIZE];
  Field **field;
  String query(query_buffer, sizeof(query_buffer), &my_charset_bin);
  FEDERATEDX_SHARE *share= NULL, tmp_share;
  MEM_ROOT mem_root;
  DBUG_ENTER("ha_federatedx.cc::get_share");

  /*
    In order to use this string, we must first zero it's length,
    or it will contain garbage
  */
  query.length(0);

  bzero(&tmp_share, sizeof(tmp_share));
  init_alloc_root(&mem_root, "federated", 256, 0, MYF(0));

  mysql_mutex_lock(&federatedx_mutex);

  if (unlikely(!UTC))
  {
    String tz_00_name(STRING_WITH_LEN("+00:00"), &my_charset_bin);
    UTC= my_tz_find(current_thd, &tz_00_name);
  }

  tmp_share.share_key= table_name;
  tmp_share.share_key_length= (int)strlen(table_name);
  if (parse_url(&mem_root, &tmp_share, table->s, 0))
    goto error;

  /* TODO: change tmp_share.scheme to LEX_STRING object */
  if (!(share= (FEDERATEDX_SHARE *) my_hash_search(&federatedx_open_tables,
                                               (uchar*) tmp_share.share_key,
                                               tmp_share.
                                               share_key_length)))
  {
    query.set_charset(system_charset_info);
    query.append(STRING_WITH_LEN("SELECT "));
    for (field= table->field; *field; field++)
    {
      append_ident(&query, (*field)->field_name.str,
                   (*field)->field_name.length, ident_quote_char);
      query.append(STRING_WITH_LEN(", "));
    }
    /* chops off trailing comma */
    query.length(query.length() - sizeof_trailing_comma);

    query.append(STRING_WITH_LEN(" FROM "));

    append_ident(&query, tmp_share.table_name,
                 tmp_share.table_name_length, ident_quote_char);

    if (!(share= (FEDERATEDX_SHARE *) memdup_root(&mem_root, (char*)&tmp_share, sizeof(*share))) ||
        !(share->share_key= (char*) memdup_root(&mem_root, tmp_share.share_key, tmp_share.share_key_length+1)) ||
        !(share->select_query= (char*) strmake_root(&mem_root, query.ptr(), query.length())))
      goto error;

    share->mem_root= mem_root;

    DBUG_PRINT("info",
               ("share->select_query %s", share->select_query));

    if (!(share->s= get_server(share, table)))
      goto error;
   
    if (my_hash_insert(&federatedx_open_tables, (uchar*) share))
      goto error;
    thr_lock_init(&share->lock);
  }
  else
    free_root(&mem_root, MYF(0)); /* prevents memory leak */

  share->use_count++;
  mysql_mutex_unlock(&federatedx_mutex);

  DBUG_RETURN(share);

error:
  mysql_mutex_unlock(&federatedx_mutex);
  free_root(&mem_root, MYF(0));
  DBUG_RETURN(NULL);
}


static federatedx_txn zero_txn;
static int free_server(federatedx_txn *txn, FEDERATEDX_SERVER *server)
{
  bool destroy;
  DBUG_ENTER("free_server");

  mysql_mutex_lock(&federatedx_mutex);
  if ((destroy= !--server->use_count))
    my_hash_delete(&federatedx_open_servers, (uchar*) server);
  mysql_mutex_unlock(&federatedx_mutex);

  if (destroy)
  {
    MEM_ROOT mem_root;

    if (!txn)
      txn= &zero_txn;

    txn->close(server);

    DBUG_ASSERT(server->io_count == 0);

    mysql_mutex_destroy(&server->mutex);
    mem_root= server->mem_root;
    free_root(&mem_root, MYF(0));
  }

  DBUG_RETURN(0);
}


/*
  Free lock controls. We call this whenever we close a table.
  If the table had the last reference to the share then we
  free memory associated with it.
*/

static void free_share(federatedx_txn *txn, FEDERATEDX_SHARE *share)
{
  bool destroy;
  DBUG_ENTER("free_share");

  mysql_mutex_lock(&federatedx_mutex);
  if ((destroy= !--share->use_count))
    my_hash_delete(&federatedx_open_tables, (uchar*) share);
  mysql_mutex_unlock(&federatedx_mutex);

  if (destroy)
  {
    MEM_ROOT mem_root;
    FEDERATEDX_SERVER *server= share->s;

    thr_lock_delete(&share->lock);

    mem_root= share->mem_root;
    free_root(&mem_root, MYF(0));

    free_server(txn, server);
  }

  DBUG_VOID_RETURN;
}

enum range_type {
    EQUAL = 0,
    ONE_WAY_RANGE,
    TWO_WAY_RANGE,
    NONE,
};

bool compare_key_part_and_extract_actual_string_length(KEY_PART_INFO *key_part, const uchar *start_ptr,
                                                       const uchar *end_ptr, uint length, uint *actual_string_length) {
  bool ret = true;
  if (key_part->key_part_flag & HA_VAR_LENGTH_PART) {
    // for var length key, compare the const with actual length
    uint start_length = uint2korr(start_ptr);
    uint end_length = uint2korr(end_ptr);
    if (start_length != end_length) {
      ret = false;
    } else {
      for (uint i = 0; i < start_length; i++) {
        if (start_ptr[i + HA_KEY_BLOB_LENGTH] != end_ptr[i + HA_KEY_BLOB_LENGTH]) {
          ret = false;
          break;
        }
      }
    }
  } else {
    for (uint i = 0; i < length; i++) {
      if (start_ptr[i] != end_ptr[i]) {
        ret = false;
        break;
      }
    }
  }

  if (key_part->key_part_flag & HA_VAR_LENGTH_PART) {
    uint start_length = uint2korr(start_ptr);
    uint end_length = uint2korr(end_ptr);
    uint common_length = start_length > end_length ? end_length : start_length;
    uint actual_length = 0;
    uint i = 0;
    for (i = 0; i < common_length; i++) {
      if (start_ptr[i + HA_KEY_BLOB_LENGTH] != end_ptr[i + HA_KEY_BLOB_LENGTH] || start_ptr[i + HA_KEY_BLOB_LENGTH] == 0 || end_ptr[i + HA_KEY_BLOB_LENGTH] == 0) {
        actual_length = i;
        break;
      }
    }
    if (i == common_length) {
      actual_length = common_length;
    }
    if (actual_length > 0) {
      *actual_string_length = actual_length;
    }
  } else if (key_part->field->type() == MYSQL_TYPE_STRING) {
    uint actual_length = 0;
    uint i = 0;
    for (i = 0; i < length; i++) {
      if (start_ptr[i] != end_ptr[i] || start_ptr[i] == 0 || end_ptr[i] == 0) {
        actual_length = i;
        break;
      }
    }
    if (i == length) {
      actual_length = length;
    }
    if (actual_length > 0) {
      *actual_string_length = actual_length;
    }
  }
  return ret;
}

bool key_range_is_null(KEY *key_info, key_range *key) {
  if (key == NULL) {
    return true;
  }
  return key_info->key_part->null_bit && key->key[0];
}

bool is_isnull_filter(KEY *key_info, key_range *start_key, key_range *end_key) {
  if (start_key == NULL || end_key == NULL) {
    return false;
  }
  if (start_key->length != end_key->length) {
    return false;
  }
  return key_range_is_null(key_info, start_key) && key_range_is_null(key_info, end_key);
  /*uint remainder, length, value_index;
  KEY_PART_INFO *key_part;
  const uchar *start_ptr;
  const uchar *end_ptr;
  for (key_part = key_info->key_part, remainder = key_info->user_defined_key_parts,
          length = start_key->length, start_ptr = start_key->key,
          end_ptr = end_key->key, value_index = 0; ; remainder--,key_part++,value_index++) {
      uint store_length = key_part->store_length;
      bool start_null = false, end_null = false;
      if (key_part->null_bit) {
        if (*start_ptr++) {
          start_null = true;
        }
        if (*end_ptr++) {
          end_null = true;
        }
        if (start_null && end_null) {
          return true;
        }
      }

      if (store_length >= length) {
        break;
      }
      length -= store_length;
      start_ptr += store_length - MY_TEST(key_part->null_bit);
      end_ptr += store_length - MY_TEST(key_part->null_bit);
  }*/
  return false;
}

bool compare_key_and_extract_actual_string_length(KEY *key_info, key_range *start_key,
                                                  key_range *end_key, uint *actual_string_length) {
  if (start_key->length != end_key->length) {
    return false;
  }
  uint remainder, length, value_index;
  KEY_PART_INFO *key_part;
  bool ret = true;
  const uchar *start_ptr;
  const uchar *end_ptr;
  for (key_part = key_info->key_part, remainder = key_info->user_defined_key_parts,
          length = start_key->length, start_ptr = start_key->key,
          end_ptr = end_key->key, value_index = 0; ; remainder--,key_part++,value_index++) {
      uint store_length = key_part->store_length;
      uint part_length= MY_MIN(store_length, length);
      bool start_null = false, end_null = false;
      if (key_part->null_bit) {
        if (*start_ptr++) {
          start_null = true;
        }
        if (*end_ptr++) {
          end_null = true;
        }
        if (start_null && end_null) {
          goto for_next_key_part;
        }
      }

      if (!start_null && !end_null) {
        //both not null
        bool same = compare_key_part_and_extract_actual_string_length(key_part, start_ptr, end_ptr, part_length, actual_string_length);
        ret &= same;
      } else {
        ret = false;
      }

for_next_key_part:
      if (store_length >= length) {
        break;
      }
      length -= store_length;
      start_ptr += store_length - MY_TEST(key_part->null_bit);
      end_ptr += store_length - MY_TEST(key_part->null_bit);
  }
  return ret;
}

range_type get_range_type(KEY *key_info, key_range *start_key, key_range *end_key, uint *actual_key_length) {
  bool start_key_null = key_range_is_null(key_info, start_key);
  bool end_key_null = key_range_is_null(key_info, end_key);
  if (!start_key_null && !end_key_null) {
    bool same_key = compare_key_and_extract_actual_string_length(key_info, start_key, end_key, actual_key_length);
    if (same_key) {
      return EQUAL;
    } else {
      return TWO_WAY_RANGE;
    }
  } else if (!start_key_null) {
    if (start_key->flag != HA_READ_KEY_EXACT) {
      return ONE_WAY_RANGE;
    }
    return EQUAL;
  } else if (!end_key_null) {
    if (end_key->flag != HA_READ_KEY_EXACT) {
      return ONE_WAY_RANGE;
    }
    return EQUAL;
  }
  return NONE;
}

bool ha_federatedx::is_valid_index(uint inx) {
  DBUG_ENTER("ha_federatedx::is_valid_index");
  if (ha_thd() && optimizer_flag(ha_thd(), OPTIMIZER_SWITCH_FEDX_CBO_WITH_ACTUAL_RECORDS) && index_cardinality_init) {
    ha_rows cardinality = index_cardinality[inx];
    ha_rows valid_index_percent = ha_thd()->variables.fedx_valid_index_cardinality_percent;
    ha_rows valid_index_minvalue = ha_thd()->variables.fedx_valid_index_cardinality_minvalue;
    if (cardinality == 0 || cardinality * 10000 < records_per_shard * valid_index_percent
        || cardinality < valid_index_minvalue) {
      DBUG_RETURN(false);
    } else {
      DBUG_RETURN(true);
    }
  } else {
    DBUG_RETURN(true);
  }
}

ha_rows ha_federatedx::records_in_range(uint inx, key_range *start_key,
                                       key_range *end_key)
{
  /*

  We really want indexes to be used as often as possible, therefore
  we just need to hard-code the return value to a very low number to
  force the issue

*/
  DBUG_ENTER("ha_federatedx::records_in_range");
  if (is_isnull_filter(&table->key_info[inx], start_key, end_key)) {
    // there is a bug for is null index read:
    // delete from xxx where key is null
    // when enable is null filter read, the access type is 'range',
    // and the generated filter(by create_where_from_key) is
    // xx is null and xx is not null
    // do not found root cause so far, just disable is null filter to avoid wrong result
    DBUG_RETURN(HA_POS_ERROR);
  }
  if (ha_thd() && optimizer_flag(ha_thd(), OPTIMIZER_SWITCH_FEDX_CBO_WITH_ACTUAL_RECORDS) && index_cardinality_init) {
    ha_rows index_one_way_percent = ha_thd()->variables.fedx_index_one_way_percent;
    ha_rows index_two_way_percent = ha_thd()->variables.fedx_index_two_way_percent;
    ha_rows invalid_index_expand_factor = ha_thd()->variables.fedx_invalid_index_expand_factor;
    ha_rows valid_index_max_result_rowcount = ha_thd()->variables.fedx_valid_index_max_result_rowcount;
    ha_rows small_table_threshold = ha_thd()->variables.fedx_small_table_threshold;
    if (stats.records <= small_table_threshold) {
      DBUG_RETURN(FEDERATEDX_RECORDS_IN_RANGE);
    }
    if (!is_valid_index(inx)) {
      // we really do not want to use invalid index, so just return invalid_index_expand_factor*stats.records
      DBUG_RETURN(stats.records*invalid_index_expand_factor < FEDERATEDX_RECORDS_IN_RANGE ?
                  FEDERATEDX_RECORDS_IN_RANGE : stats.records*invalid_index_expand_factor);
    }

    range_type type;
    uint actual_key_length = 0;
    type = get_range_type(&table->key_info[inx], start_key, end_key, &actual_key_length);
    if (actual_key_length < ha_thd()->variables.fedx_vitess_min_str_len_for_cbo) {
      actual_key_length = 0;
    }
    ha_rows ret;
      if (type == EQUAL) {
        ret = stats.records/index_cardinality[inx];
      } else if (type == ONE_WAY_RANGE) {
        ret = stats.records * index_one_way_percent /100;
      } else if (type == TWO_WAY_RANGE) {
        if (actual_key_length != 0) {
          ha_rows zero_length_value = stats.records * index_two_way_percent / 100;
          ha_rows n_length_value = stats.records / index_cardinality[inx];
          uint key_length = start_key->length;
          //todo this is for utf8 charset, we should find a better way to figure out the actual key length
          if (actual_key_length * 3 > key_length) {
            actual_key_length = key_length;
          } else {
            actual_key_length = actual_key_length * 3;
          }
          double total = zero_length_value / (double) n_length_value;
          double scale = pow(total, actual_key_length/(double)key_length);
          scale = total / scale;
          double actual_rows = n_length_value * scale;
          ret = (ha_rows) actual_rows;
          if (ret < n_length_value) {
            ret = n_length_value;
          }
        } else {
          ret = stats.records * index_two_way_percent/100;
        }
      } else {
        ret = stats.records;
      }
    if (ret >= valid_index_max_result_rowcount) {
      // if the result rowcount is too large, do not use it as a federated index
      DBUG_RETURN(stats.records*invalid_index_expand_factor < FEDERATEDX_RECORDS_IN_RANGE ?
                  FEDERATEDX_RECORDS_IN_RANGE : stats.records*invalid_index_expand_factor);
    }

    if (ret < FEDERATEDX_RECORDS_IN_RANGE) {
      ret = FEDERATEDX_RECORDS_IN_RANGE;
    }
    DBUG_RETURN(ret);
  }
  DBUG_RETURN(FEDERATEDX_RECORDS_IN_RANGE);
}

federatedx_txn *ha_federatedx::get_txn(THD *thd, bool no_create)
{
  federatedx_txn **txnp= (federatedx_txn **) ha_data(thd);
  if (!*txnp && !no_create)
    *txnp= new federatedx_txn();
  return *txnp;
}


int ha_federatedx::disconnect(handlerton *hton, MYSQL_THD thd)
{
  federatedx_txn *txn= (federatedx_txn *) thd_get_ha_data(thd, hton);
  delete txn;
  *((federatedx_txn **) thd_ha_data(thd, hton))= 0;
  return 0;
}


/*
  Used for opening tables. The name will be the name of the file.
  A table is opened when it needs to be opened. For instance
  when a request comes in for a select on the table (tables are not
  open and closed for each request, they are cached).

  Called from handler.cc by handler::ha_open(). The server opens
  all tables by calling ha_open() which then calls the handler
  specific open().
*/

int ha_federatedx::open(const char *name, int mode, uint test_if_locked)
{
  int error;
  THD *thd= ha_thd();
  DBUG_ENTER("ha_federatedx::open");

  if (!(share= get_share(name, table)))
    DBUG_RETURN(1);
  thr_lock_data_init(&share->lock, &lock, NULL);

  DBUG_ASSERT(io == NULL);

  txn= get_txn(thd);

  if ((error= txn->acquire(share, thd, TRUE, &io)))
  {
    free_share(txn, share);
    DBUG_RETURN(error);
  }

  ref_length= (uint)io->get_ref_length();

  txn->release(&io);

  DBUG_PRINT("info", ("ref_length: %u", ref_length));

  my_init_dynamic_array(&results, sizeof(FEDERATEDX_IO_RESULT*), 4, 4, MYF(0));
  pr_info.partial_read_mode = PARTIAL_READ_NONE;
  partial_read_mode_by_hint = PARTIAL_READ_DEFAULT;
  pr_info.sharded_offset = 0;
  pr_info.range_offset = 0;

  reset();

  DBUG_RETURN(0);
}

class Net_error_handler : public Internal_error_handler
{
public:
  Net_error_handler() {}

public:
  bool handle_condition(THD *thd, uint sql_errno, const char* sqlstate,
                        Sql_condition::enum_warning_level *level,
                        const char* msg, Sql_condition ** cond_hdl)
  {
    return sql_errno >= ER_ABORTING_CONNECTION &&
           sql_errno <= ER_NET_WRITE_INTERRUPTED;
  }
};

/*
  Closes a table. We call the free_share() function to free any resources
  that we have allocated in the "shared" structure.

  Called from sql_base.cc, sql_select.cc, and table.cc.
  In sql_select.cc it is only used to close up temporary tables or during
  the process where a temporary table is converted over to being a
  myisam table.
  For sql_base.cc look at close_data_tables().
*/

int ha_federatedx::close(void)
{
  int retval= 0;
  THD *thd= ha_thd();
  DBUG_ENTER("ha_federatedx::close");

  /* free the result set */
  reset();

  delete_dynamic(&results);

  /* Disconnect from mysql */
  if (!thd || !(txn= get_txn(thd, true)))
    txn= &zero_txn;

  txn->release(&io);
  DBUG_ASSERT(io == NULL);

  Net_error_handler err_handler;
  if (thd)
    thd->push_internal_handler(&err_handler);
  free_share(txn, share);
  if (thd)
    thd->pop_internal_handler();

  DBUG_RETURN(retval);
}

/*

  Checks if a field in a record is SQL NULL.

  SYNOPSIS
    field_in_record_is_null()
      table     TABLE pointer, MySQL table object
      field     Field pointer, MySQL field object
      record    char pointer, contains record

    DESCRIPTION
      This method uses the record format information in table to track
      the null bit in record.

    RETURN VALUE
      1    if NULL
      0    otherwise
*/

static inline uint field_in_record_is_null(TABLE *table, Field *field,
                                           char *record)
{
  int null_offset;
  DBUG_ENTER("ha_federatedx::field_in_record_is_null");

  if (!field->null_ptr)
    DBUG_RETURN(0);

  null_offset= (uint) ((char*)field->null_ptr - (char*)table->record[0]);

  if (record[null_offset] & field->null_bit)
    DBUG_RETURN(1);

  DBUG_RETURN(0);
}


/**
  @brief Construct the INSERT statement.
  
  @details This method will construct the INSERT statement and appends it to
  the supplied query string buffer.
  
  @return
    @retval FALSE       No error
    @retval TRUE        Failure
*/

bool ha_federatedx::append_stmt_insert(String *query)
{
  char insert_buffer[FEDERATEDX_QUERY_BUFFER_SIZE];
  Field **field;
  uint tmp_length;
  bool added_field= FALSE;

  /* The main insert query string */
  String insert_string(insert_buffer, sizeof(insert_buffer), &my_charset_bin);
  DBUG_ENTER("ha_federatedx::append_stmt_insert");

  insert_string.length(0);

  if (replace_duplicates)
    insert_string.append(STRING_WITH_LEN("REPLACE INTO "));
  else if (ignore_duplicates && !insert_dup_update)
    insert_string.append(STRING_WITH_LEN("INSERT IGNORE INTO "));
  else
    insert_string.append(STRING_WITH_LEN("INSERT INTO "));
  append_ident(&insert_string, share->table_name, share->table_name_length,
               ident_quote_char);
  tmp_length= insert_string.length();
  insert_string.append(STRING_WITH_LEN(" ("));

  /*
    loop through the field pointer array, add any fields to both the values
    list and the fields list that match the current query id
  */
  for (field= table->field; *field; field++)
  {
    if (bitmap_is_set(table->write_set, (*field)->field_index))
    {
      /* append the field name */
      append_ident(&insert_string, (*field)->field_name.str,
                   (*field)->field_name.length, ident_quote_char);

      /* append commas between both fields and fieldnames */
      /*
        unfortunately, we can't use the logic if *(fields + 1) to
        make the following appends conditional as we don't know if the
        next field is in the write set
      */
      insert_string.append(STRING_WITH_LEN(", "));
      added_field= TRUE;
    }
  }

  if (added_field)
  {
    /* Remove trailing comma. */
    insert_string.length(insert_string.length() - sizeof_trailing_comma);
    insert_string.append(STRING_WITH_LEN(") "));
  }
  else
  {
    /* If there were no fields, we don't want to add a closing paren. */
    insert_string.length(tmp_length);
  }

  insert_string.append(STRING_WITH_LEN(" VALUES "));

  DBUG_RETURN(query->append(insert_string));
}


/*
  write_row() inserts a row. No extra() hint is given currently if a bulk load
  is happeneding. buf() is a byte array of data. You can use the field
  information to extract the data from the native byte array type.
  Example of this would be:
  for (Field **field=table->field ; *field ; field++)
  {
    ...
  }

  Called from item_sum.cc, item_sum.cc, sql_acl.cc, sql_insert.cc,
  sql_insert.cc, sql_select.cc, sql_table.cc, sql_udf.cc, and sql_update.cc.
*/

int ha_federatedx::write_row(uchar *buf)
{
  char values_buffer[FEDERATEDX_QUERY_BUFFER_SIZE];
  char insert_field_value_buffer[STRING_BUFFER_USUAL_SIZE];
  Field **field;
  uint tmp_length;
  int error= 0;
  bool use_bulk_insert;
  ha_rows bulk_insert_max_size = ha_thd()->variables.fedx_batch_insert_size;
  bool auto_increment_update_required= (table->next_number_field != NULL);

  /* The string containing the values to be added to the insert */
  String values_string(values_buffer, sizeof(values_buffer), &my_charset_bin);
  /* The actual value of the field, to be added to the values_string */
  String insert_field_value_string(insert_field_value_buffer,
                                   sizeof(insert_field_value_buffer),
                                   &my_charset_bin);
  Time_zone *saved_time_zone= table->in_use->variables.time_zone;
  my_bitmap_map *old_map= dbug_tmp_use_all_columns(table, table->read_set);
  DBUG_ENTER("ha_federatedx::write_row");

  table->in_use->variables.time_zone= UTC;
  values_string.length(0);
  insert_field_value_string.length(0);

  /*
    start both our field and field values strings
    We must disable multi-row insert for "INSERT...ON DUPLICATE KEY UPDATE"
    Ignore duplicates is always true when insert_dup_update is true.
    When replace_duplicates == TRUE, we can safely enable multi-row insert.
    When performing multi-row insert, we only collect the columns values for
    the row. The start of the statement is only created when the first
    row is copied in to the bulk_insert string.
  */
  if (!(use_bulk_insert= bulk_insert.str && 
        (!insert_dup_update || replace_duplicates)))
    append_stmt_insert(&values_string);

  values_string.append(STRING_WITH_LEN(" ("));
  tmp_length= values_string.length();

  /*
    loop through the field pointer array, add any fields to both the values
    list and the fields list that is part of the write set
  */
  for (field= table->field; *field; field++)
  {
    if (bitmap_is_set(table->write_set, (*field)->field_index))
    {
      if ((*field)->is_null())
        values_string.append(STRING_WITH_LEN(" NULL "));
      else
      {
        bool needs_quote= (*field)->str_needs_quotes();
        (*field)->val_str(&insert_field_value_string);
        if (needs_quote)
          values_string.append(value_quote_char);
        insert_field_value_string.print(&values_string);
        if (needs_quote)
          values_string.append(value_quote_char);

        insert_field_value_string.length(0);
      }

      /* append commas between both fields and fieldnames */
      /*
        unfortunately, we can't use the logic if *(fields + 1) to
        make the following appends conditional as we don't know if the
        next field is in the write set
      */
      values_string.append(STRING_WITH_LEN(", "));
    }
  }
  dbug_tmp_restore_column_map(table->read_set, old_map);
  table->in_use->variables.time_zone= saved_time_zone;

  /*
    if there were no fields, we don't want to add a closing paren
    AND, we don't want to chop off the last char '('
    insert will be "INSERT INTO t1 VALUES ();"
  */
  if (values_string.length() > tmp_length)
  {
    /* chops off trailing comma */
    values_string.length(values_string.length() - sizeof_trailing_comma);
  }
  /* we always want to append this, even if there aren't any fields */
  values_string.append(STRING_WITH_LEN(") "));

  if ((error= txn->acquire(share, ha_thd(), FALSE, &io)))
    DBUG_RETURN(error);

  if (use_bulk_insert)
  {
    /*
      Send the current bulk insert out if appending the current row would
      cause the statement to overflow the packet size, otherwise set
      auto_increment_update_required to FALSE as no query was executed.
    */
    if ((bulk_insert.length + values_string.length() + bulk_padding >
        io->max_query_size() || (bulk_insert_max_size > 0
        && bulk_insert_size >= bulk_insert_max_size)) && bulk_insert.length)
    {
      error= io->query(bulk_insert.str, bulk_insert.length, SCAN_MODE_OLTP, NULL);
      if(error == 0) {
        insert_records_since_init += bulk_insert_size;
      }
      bulk_insert.length= 0;
      bulk_insert_size = 0;
    }
    else {
      auto_increment_update_required = FALSE;
    }
      
    if (bulk_insert.length == 0)
    {
      char insert_buffer[FEDERATEDX_QUERY_BUFFER_SIZE];
      String insert_string(insert_buffer, sizeof(insert_buffer), 
                           &my_charset_bin);
      insert_string.length(0);
      append_stmt_insert(&insert_string);
      dynstr_append_mem(&bulk_insert, insert_string.ptr(), 
                        insert_string.length());
    }
    else
      dynstr_append_mem(&bulk_insert, ",", 1);

    dynstr_append_mem(&bulk_insert, values_string.ptr(), 
                      values_string.length());
    bulk_insert_size++;
  }
  else
  {
    error= io->query(values_string.ptr(), values_string.length(), SCAN_MODE_OLTP, NULL);
    if(error == 0) {
      insert_records_since_init++;
    }
  }
  
  if (error)
  {
    DBUG_RETURN(stash_remote_error());
  }
  /*
    If the table we've just written a record to contains an auto_increment
    field, then store the last_insert_id() value from the foreign server
  */
  if (auto_increment_update_required)
  {
    update_auto_increment();

    /* mysql_insert() uses this for protocol return value */
    table->next_number_field->store(stats.auto_increment_value, 1);
  }

  DBUG_RETURN(0);
}


/**
  @brief Prepares the storage engine for bulk inserts.
  
  @param[in] rows       estimated number of rows in bulk insert 
                        or 0 if unknown.
  
  @details Initializes memory structures required for bulk insert.
*/

void ha_federatedx::start_bulk_insert(ha_rows rows, uint flags)
{
  uint page_size;
  DBUG_ENTER("ha_federatedx::start_bulk_insert");

  dynstr_free(&bulk_insert);
  
  /**
    We don't bother with bulk-insert semantics when the estimated rows == 1
    The rows value will be 0 if the server does not know how many rows
    would be inserted. This can occur when performing INSERT...SELECT
  */
  
  if (rows == 1)
    DBUG_VOID_RETURN;

  /*
    Make sure we have an open connection so that we know the 
    maximum packet size.
  */
  if (txn->acquire(share, ha_thd(), FALSE, &io))
    DBUG_VOID_RETURN;

  page_size= (uint) my_getpagesize();

  if (init_dynamic_string(&bulk_insert, NULL, page_size, page_size))
    DBUG_VOID_RETURN;
  
  bulk_insert.length= 0;
  bulk_insert_size = 0;
  DBUG_VOID_RETURN;
}


/**
  @brief End bulk insert.
  
  @details This method will send any remaining rows to the remote server.
  Finally, it will deinitialize the bulk insert data structure.
  
  @return Operation status
  @retval       0       No error
  @retval       != 0    Error occurred at remote server. Also sets my_errno.
*/

int ha_federatedx::end_bulk_insert()
{
  int error= 0;
  DBUG_ENTER("ha_federatedx::end_bulk_insert");
  
  if (bulk_insert.str && bulk_insert.length && !table_will_be_deleted)
  {
    if ((error= txn->acquire(share, ha_thd(), FALSE, &io)))
      DBUG_RETURN(error);
    if (io->query(bulk_insert.str, bulk_insert.length, SCAN_MODE_OLTP, NULL))
      error= stash_remote_error();
    else
    if (table->next_number_field)
      update_auto_increment();
    if(error == 0) {
      insert_records_since_init += bulk_insert_size;
    }
  }

  dynstr_free(&bulk_insert);
  bulk_insert_size = 0;
  
  DBUG_RETURN(my_errno= error);
}


/*
  ha_federatedx::update_auto_increment

  This method ensures that last_insert_id() works properly. What it simply does
  is calls last_insert_id() on the foreign database immediately after insert
  (if the table has an auto_increment field) and sets the insert id via
  thd->insert_id(ID)).
*/
void ha_federatedx::update_auto_increment(void)
{
  THD *thd= ha_thd();
  DBUG_ENTER("ha_federatedx::update_auto_increment");

  ha_federatedx::info(HA_STATUS_AUTO);
  thd->first_successful_insert_id_in_cur_stmt= 
    stats.auto_increment_value;
  DBUG_PRINT("info",("last_insert_id: %ld", (long) stats.auto_increment_value));

  DBUG_VOID_RETURN;
}

int ha_federatedx::optimize(THD* thd, HA_CHECK_OPT* check_opt)
{
  int error= 0;
  char query_buffer[STRING_BUFFER_USUAL_SIZE];
  String query(query_buffer, sizeof(query_buffer), &my_charset_bin);
  DBUG_ENTER("ha_federatedx::optimize");
  
  query.length(0);

  query.set_charset(system_charset_info);
  query.append(STRING_WITH_LEN("OPTIMIZE TABLE "));
  append_ident(&query, share->table_name, share->table_name_length,
               ident_quote_char);

  DBUG_ASSERT(txn == get_txn(thd));

  if ((error= txn->acquire(share, thd, FALSE, &io)))
    DBUG_RETURN(error);

  if (io->query(query.ptr(), query.length(), SCAN_MODE_EITHER, NULL))
    error= stash_remote_error();

  DBUG_RETURN(error);
}


int ha_federatedx::repair(THD* thd, HA_CHECK_OPT* check_opt)
{
  int error= 0;
  char query_buffer[STRING_BUFFER_USUAL_SIZE];
  String query(query_buffer, sizeof(query_buffer), &my_charset_bin);
  DBUG_ENTER("ha_federatedx::repair");

  query.length(0);

  query.set_charset(system_charset_info);
  query.append(STRING_WITH_LEN("REPAIR TABLE "));
  append_ident(&query, share->table_name, share->table_name_length,
               ident_quote_char);
  if (check_opt->flags & T_QUICK)
    query.append(STRING_WITH_LEN(" QUICK"));
  if (check_opt->flags & T_EXTEND)
    query.append(STRING_WITH_LEN(" EXTENDED"));
  if (check_opt->sql_flags & TT_USEFRM)
    query.append(STRING_WITH_LEN(" USE_FRM"));

  DBUG_ASSERT(txn == get_txn(thd));

  if ((error= txn->acquire(share, thd, FALSE, &io)))
    DBUG_RETURN(error);

  if (io->query(query.ptr(), query.length(), SCAN_MODE_EITHER, NULL))
    error= stash_remote_error();

  DBUG_RETURN(error);
}


/*
  Yes, update_row() does what you expect, it updates a row. old_data will have
  the previous row record in it, while new_data will have the newest data in
  it.

  Keep in mind that the server can do updates based on ordering if an ORDER BY
  clause was used. Consecutive ordering is not guaranteed.
  Currently new_data will not have an updated auto_increament record, or
  and updated timestamp field. You can do these for federatedx by doing these:
  if (table->timestamp_on_update_now)
    update_timestamp(new_row+table->timestamp_on_update_now-1);
  if (table->next_number_field && record == table->record[0])
    update_auto_increment();

  Called from sql_select.cc, sql_acl.cc, sql_update.cc, and sql_insert.cc.
*/

int ha_federatedx::update_row(const uchar *old_data, const uchar *new_data)
{
  /*
    This used to control how the query was built. If there was a
    primary key, the query would be built such that there was a where
    clause with only that column as the condition. This is flawed,
    because if we have a multi-part primary key, it would only use the
    first part! We don't need to do this anyway, because
    read_range_first will retrieve the correct record, which is what
    is used to build the WHERE clause. We can however use this to
    append a LIMIT to the end if there is NOT a primary key. Why do
    this? Because we only are updating one record, and LIMIT enforces
    this.
  */
  bool has_a_primary_key= MY_TEST(table->s->primary_key != MAX_KEY);
  bool use_pk_in_filter = false;
  if (vindex_init && pk_num > 0 && ha_thd()->variables.fedx_pk_update_delete_level) {
    ha_rows pk_level = ha_thd()->variables.fedx_pk_update_delete_level;
    if (pk_level == 2) {
      use_pk_in_filter = true;
    } else if (pk_level == 1 && pk_num == 1) {
      use_pk_in_filter = true;
    }
  }

  if (use_pk_in_filter) {
    if (!bitmap_is_subset(&pk_set, table->read_set)) {
      // should not reach here
      use_pk_in_filter = false;
    }
  }

  bool need_convert = false;
  if (has_a_primary_key && vindex_init && bitmap_is_overlapping(table->write_set, &vindex_set)
          && bitmap_is_set_all(table->read_set)) {
    // update vindex column for vitess table, which is not allowed in vitess
    // we convert the update to insert and delete
    need_convert = true;
  }

  /*
    buffers for following strings
  */
  char field_value_buffer[STRING_BUFFER_USUAL_SIZE];
  char update_buffer[FEDERATEDX_QUERY_BUFFER_SIZE];
  char where_buffer[FEDERATEDX_QUERY_BUFFER_SIZE];
  char insert_buffer[STRING_BUFFER_USUAL_SIZE];
  char values_buffer[FEDERATEDX_QUERY_BUFFER_SIZE];
  char delete_buffer[FEDERATEDX_QUERY_BUFFER_SIZE];
  char tmp_buffer[FEDERATEDX_QUERY_BUFFER_SIZE];

  /* Work area for field values */
  String field_value(field_value_buffer, sizeof(field_value_buffer),
                     &my_charset_bin);
  /* stores the update query */
  String update_string(update_buffer,
                       sizeof(update_buffer),
                       &my_charset_bin);
  /* stores the WHERE clause */
  String where_string(where_buffer,
                      sizeof(where_buffer),
                      &my_charset_bin);
  /* stores the insert clause */
  String insert_string(insert_buffer,
                      sizeof(insert_buffer),
                      &my_charset_bin);
  /* stores the values clause */
  String values_string(values_buffer,
                      sizeof(values_buffer),
                      &my_charset_bin);
  /* stores the delete query */
  String delete_string(delete_buffer,
                       sizeof(delete_buffer),
                       &my_charset_bin);
  String tmp_string(tmp_buffer,
                       sizeof(tmp_buffer),
                       &my_charset_bin);
  uchar *record= table->record[0];
  int error;
  DBUG_ENTER("ha_federatedx::update_row");
  /*
    set string lengths to 0 to avoid misc chars in string
  */
  field_value.length(0);
  update_string.length(0);
  where_string.length(0);
  insert_string.length(0);
  values_string.length(0);
  delete_string.length(0);
  tmp_string.length(0);

  if (ignore_duplicates)
    update_string.append(STRING_WITH_LEN("UPDATE IGNORE "));
  else
    update_string.append(STRING_WITH_LEN("UPDATE "));
  append_ident(&update_string, share->table_name,
               share->table_name_length, ident_quote_char);
  update_string.append(STRING_WITH_LEN(" SET "));

  if (need_convert) {
    insert_string.append("INSERT INTO ");
    append_ident(&insert_string, share->table_name,
                 share->table_name_length, ident_quote_char);
    insert_string.append(STRING_WITH_LEN("("));
    delete_string.append("DELETE FROM ");
    append_ident(&delete_string, share->table_name,
                 share->table_name_length, ident_quote_char);
  }

  /*
    In this loop, we want to match column names to values being inserted
    (while building INSERT statement).

    Iterate through table->field (new data) and share->old_field (old_data)
    using the same index to create an SQL UPDATE statement. New data is
    used to create SET field=value and old data is used to create WHERE
    field=oldvalue
  */

  Time_zone *saved_time_zone= table->in_use->variables.time_zone;
  table->in_use->variables.time_zone= UTC;
  for (Field **field= table->field; *field; field++)
  {
    if (need_convert) {
      append_ident(&insert_string, (*field)->field_name.str,
                   (*field)->field_name.length, ident_quote_char);
      insert_string.append(STRING_WITH_LEN(", "));
    }
    bool value_set = false;
    if (bitmap_is_set(table->write_set, (*field)->field_index))
    {
      value_set = true;
      append_ident(&update_string, (*field)->field_name.str,
                   (*field)->field_name.length,
                   ident_quote_char);
      update_string.append(STRING_WITH_LEN(" = "));
      tmp_string.length(0);

      if ((*field)->is_null()) {
        tmp_string.append(STRING_WITH_LEN(" NULL "));
      }
      else
      {
        /* otherwise = */
        my_bitmap_map *old_map= tmp_use_all_columns(table, table->read_set);
        bool needs_quote= (*field)->str_needs_quotes();
	    (*field)->val_str(&field_value);

        if (needs_quote)
          tmp_string.append(value_quote_char);
        field_value.print(&tmp_string);
        if (needs_quote)
          tmp_string.append(value_quote_char);

        field_value.length(0);
        tmp_restore_column_map(table->read_set, old_map);
      }

      update_string.append(tmp_string);
      if (need_convert) {
        values_string.append(tmp_string);
      }
      update_string.append(STRING_WITH_LEN(", "));
    }

    if (bitmap_is_set(table->read_set, (*field)->field_index))
    {
      bool need_add_to_where = true;
      if (use_pk_in_filter && !(bitmap_is_set(&pk_set, (*field)->field_index) ||
              bitmap_is_set(&vindex_set, (*field)->field_index))) {
        need_add_to_where = false;
      }

      if (need_add_to_where) {
        append_ident(&where_string, (*field)->field_name.str,
                     (*field)->field_name.length,
                     ident_quote_char);
      }

      if (field_in_record_is_null(table, *field, (char*) old_data)) {
        if (need_add_to_where) {
          where_string.append(STRING_WITH_LEN(" IS NULL "));
        }
        if (!value_set && need_convert) {
          values_string.append(STRING_WITH_LEN(" NULL "));
        }
      }
      else
      {
        tmp_string.length(0);
        bool needs_quote= (*field)->str_needs_quotes();
        (*field)->val_str(&field_value,
                          (old_data + (*field)->offset(record)));

        if (needs_quote)
          tmp_string.append(value_quote_char);
        field_value.print(&tmp_string);
        if (needs_quote)
          tmp_string.append(value_quote_char);

        field_value.length(0);
        if (need_add_to_where) {
          where_string.append(STRING_WITH_LEN(" = "));
          where_string.append(tmp_string);
        }
        if (!value_set && need_convert) {
          values_string.append(tmp_string);
        }
      }
      if (need_add_to_where) {
        where_string.append(STRING_WITH_LEN(" AND "));
      }
    }

    if (need_convert) {
      values_string.append(STRING_WITH_LEN(", "));
    }
  }
  table->in_use->variables.time_zone= saved_time_zone;

  /* Remove last ', '. This works as there must be at least on updated field */
  update_string.length(update_string.length() - sizeof_trailing_comma);

  if (need_convert) {
    values_string.length(values_string.length() - sizeof_trailing_comma);
    insert_string.length(insert_string.length() - sizeof_trailing_comma);
    insert_string.append(STRING_WITH_LEN(")"));
  }

  if (where_string.length())
  {
    /* chop off trailing AND */
    where_string.length(where_string.length() - sizeof_trailing_and);
    update_string.append(STRING_WITH_LEN(" WHERE "));
    update_string.append(where_string);
    if (need_convert) {
      delete_string.append(STRING_WITH_LEN(" WHERE "));
      delete_string.append(where_string);
    }
  }

  /*
    If this table has not a primary key, then we could possibly
    update multiple rows. We want to make sure to only update one!
  */
  if (!has_a_primary_key)
    update_string.append(STRING_WITH_LEN(" LIMIT 1"));

  if ((error= txn->acquire(share, ha_thd(), FALSE, &io)))
    DBUG_RETURN(error);

  if (need_convert) {
    insert_string.append(STRING_WITH_LEN(" VALUES("));
    insert_string.append(values_string);
    insert_string.append(STRING_WITH_LEN(")"));
    if (io->query(delete_string.ptr(), delete_string.length(), SCAN_MODE_OLTP, NULL)) {
      DBUG_RETURN(stash_remote_error());
    }
    uint affected_rows = io->affected_rows();
    if (affected_rows == 1) {
      if (io->query(insert_string.ptr(), insert_string.length(), SCAN_MODE_OLTP, NULL)) {
        DBUG_RETURN(stash_remote_error());
      }
    } else if (affected_rows > 1) {
      DBUG_RETURN(HA_ERR_FOUND_DUPP_UNIQUE);
    }
  } else {
    if (io->query(update_string.ptr(), update_string.length(), SCAN_MODE_OLTP, NULL)) {
      DBUG_RETURN(stash_remote_error());
    }
  }

  update_records_since_init += 1;
  DBUG_RETURN(0);
}

/*
  This will delete a row. 'buf' will contain a copy of the row to be =deleted.
  The server will call this right after the current row has been called (from
  either a previous rnd_next() or index call).
  If you keep a pointer to the last row or can access a primary key it will
  make doing the deletion quite a bit easier.
  Keep in mind that the server does no guarentee consecutive deletions.
  ORDER BY clauses can be used.

  Called in sql_acl.cc and sql_udf.cc to manage internal table information.
  Called in sql_delete.cc, sql_insert.cc, and sql_select.cc. In sql_select
  it is used for removing duplicates while in insert it is used for REPLACE
  calls.
*/

int ha_federatedx::delete_row(const uchar *buf)
{
  char delete_buffer[FEDERATEDX_QUERY_BUFFER_SIZE];
  char data_buffer[FEDERATEDX_QUERY_BUFFER_SIZE];
  String delete_string(delete_buffer, sizeof(delete_buffer), &my_charset_bin);
  String data_string(data_buffer, sizeof(data_buffer), &my_charset_bin);
  uint found= 0;
  int error;
  DBUG_ENTER("ha_federatedx::delete_row");

  bool use_pk_in_filter = false;
  if (vindex_init && pk_num > 0 && ha_thd()->variables.fedx_pk_update_delete_level) {
    ha_rows pk_level = ha_thd()->variables.fedx_pk_update_delete_level;
    if (pk_level == 2) {
      use_pk_in_filter = true;
    } else if (pk_level == 1 && pk_num == 1) {
      use_pk_in_filter = true;
    }
  }

  if (use_pk_in_filter) {
    if (!bitmap_is_subset(&pk_set, table->read_set)) {
      // should not reach here, as we set the pk field in
      // read_set in mark_read_columns_needed_for_update_delete
      use_pk_in_filter = false;
    }
  }

  delete_string.length(0);
  delete_string.append(STRING_WITH_LEN("DELETE FROM "));
  append_ident(&delete_string, share->table_name,
               share->table_name_length, ident_quote_char);
  delete_string.append(STRING_WITH_LEN(" WHERE "));

  Time_zone *saved_time_zone= table->in_use->variables.time_zone;
  table->in_use->variables.time_zone= UTC;
  for (Field **field= table->field; *field; field++)
  {
    Field *cur_field= *field;
    bool part_of_filter = bitmap_is_set(table->read_set, cur_field->field_index);
    if (use_pk_in_filter && !(bitmap_is_set(&pk_set, cur_field->field_index) || bitmap_is_set(&vindex_set, cur_field->field_index))) {
      // if use_pk_in_filter is true, columns not in the pk/vindex is not used as part of filter
      part_of_filter = false;
    }

    if (part_of_filter)
    {
      found++;
      append_ident(&delete_string, (*field)->field_name.str,
                   (*field)->field_name.length, ident_quote_char);
      data_string.length(0);
      if (cur_field->is_null())
      {
        delete_string.append(STRING_WITH_LEN(" IS NULL "));
      }
      else
      {
        bool needs_quote= cur_field->str_needs_quotes();
        delete_string.append(STRING_WITH_LEN(" = "));
        cur_field->val_str(&data_string);
        if (needs_quote)
          delete_string.append(value_quote_char);
        data_string.print(&delete_string);
        if (needs_quote)
          delete_string.append(value_quote_char);
      }
      delete_string.append(STRING_WITH_LEN(" AND "));
    }
  }
  table->in_use->variables.time_zone= saved_time_zone;

  // Remove trailing AND
  delete_string.length(delete_string.length() - sizeof_trailing_and);
  if (!found)
    delete_string.length(delete_string.length() - sizeof_trailing_where);

  if (!use_pk_in_filter) {
    // do not need to add limit 1 since pk is used in filter
    delete_string.append(STRING_WITH_LEN(" LIMIT 1"));
  }
  DBUG_PRINT("info",
             ("Delete sql: %s", delete_string.c_ptr_quick()));

  if ((error= txn->acquire(share, ha_thd(), FALSE, &io)))
    DBUG_RETURN(error);

  if (io->query(delete_string.ptr(), delete_string.length(), SCAN_MODE_OLTP, NULL))
  {
    DBUG_RETURN(stash_remote_error());
  }
  stats.deleted+= (ha_rows) io->affected_rows();
  stats.records-= (ha_rows) io->affected_rows();
  if(stats.records < 2) {
    stats.records = 2;
  }
  delete_records_since_init += (ha_rows) io->affected_rows();
  DBUG_PRINT("info",
             ("rows deleted %ld  rows deleted for all time %ld",
              (long) io->affected_rows(), (long) stats.deleted));

  DBUG_RETURN(0);
}


/*
  Positions an index cursor to the index specified in the handle. Fetches the
  row if available. If the key value is null, begin at the first key of the
  index. This method, which is called in the case of an SQL statement having
  a WHERE clause on a non-primary key index, simply calls index_read_idx.
*/

int ha_federatedx::index_read(uchar *buf, const uchar *key,
                             uint key_len, ha_rkey_function find_flag)
{
  DBUG_ENTER("ha_federatedx::index_read");

  if (stored_result)
    (void) free_result();
  DBUG_RETURN(index_read_idx_with_result_set(buf, active_index, key,
                                             key_len, find_flag,
                                             &stored_result));
}


/*
  Positions an index cursor to the index specified in key. Fetches the
  row if any.  This is only used to read whole keys.

  This method is called via index_read in the case of a WHERE clause using
  a primary key index OR is called DIRECTLY when the WHERE clause
  uses a PRIMARY KEY index.

  NOTES
    This uses an internal result set that is deleted before function
    returns.  We need to be able to be callable from ha_rnd_pos()
*/

int ha_federatedx::index_read_idx(uchar *buf, uint index, const uchar *key,
                                 uint key_len, enum ha_rkey_function find_flag)
{
  int retval;
  FEDERATEDX_IO_RESULT *io_result= 0;
  DBUG_ENTER("ha_federatedx::index_read_idx");

  if ((retval= index_read_idx_with_result_set(buf, index, key,
                                              key_len, find_flag,
                                              &io_result)))
    DBUG_RETURN(retval);
  /* io is correct, as index_read_idx_with_result_set was ok */
  io->free_result(io_result);
  DBUG_RETURN(retval);
}


/*
  Create result set for rows matching query and return first row

  RESULT
    0	ok     In this case *result will contain the result set
    #   error  In this case *result will contain 0
*/

int ha_federatedx::index_read_idx_with_result_set(uchar *buf, uint index,
                                                 const uchar *key,
                                                 uint key_len,
                                                 ha_rkey_function find_flag,
                                                 FEDERATEDX_IO_RESULT **result)
{
  int retval;
  char error_buffer[FEDERATEDX_QUERY_ERROR_BUFFER_SIZE];
  char index_value[STRING_BUFFER_USUAL_SIZE];
  char sql_query_buffer[FEDERATEDX_QUERY_BUFFER_SIZE];
  String index_string(index_value,
                      sizeof(index_value),
                      &my_charset_bin);
  String sql_query(sql_query_buffer,
                   sizeof(sql_query_buffer),
                   &my_charset_bin);
  key_range range;
  DBUG_ENTER("ha_federatedx::index_read_idx_with_result_set");

  *result= 0;                                   // In case of errors
  index_string.length(0);
  sql_query.length(0);

  //sql_query.append(share->select_query);
  // zqdai add support for column pruning, share->select_query was used in original logic
  append_select_from(sql_query);


  range.key= key;
  range.length= key_len;
  range.flag= find_flag;
  create_where_from_key(&index_string,
                        &table->key_info[index],
                        &range,
                        NULL, 0, 0);
  sql_query.append(index_string);

  bool force_oltp = false;
  if (is_delete_update_target || statement_lock_type >= TL_READ_WITH_SHARED_LOCKS) {
    if (ha_thd()->lex->sql_command == SQLCOM_DELETE && !partial_ppd
            && ha_thd()->variables.fedx_vitess_push_limit_for_simple_dml) {
      ha_rows expand_factor = ha_thd()->variables.fedx_vitess_limit_expand_factor;
      ha_rows limit = 0;
      if (ha_thd()->lex && ha_thd()->lex->current_select) {
        limit = ha_thd()->lex->current_select->get_limit();
      }
      if (limit) {
        limit = limit * expand_factor;
        sql_query.append(STRING_WITH_LEN(" LIMIT "));
        sql_query.append_ulonglong(limit);
      }
    }
    if (statement_lock_type >= TL_WRITE_DELAYED)
        sql_query.append(STRING_WITH_LEN(" FOR UPDATE"));
    else
        sql_query.append(STRING_WITH_LEN(" LOCK IN SHARE MODE"));
    force_oltp = true;

  }
  if (!ha_thd()->transaction.all.is_empty()) {
    force_oltp = true;
  }

  if ((retval= txn->acquire(share, ha_thd(), TRUE, &io)))
    DBUG_RETURN(retval);

  if (io->query(sql_query.ptr(), sql_query.length(), force_oltp ? SCAN_MODE_OLTP : scan_mode, NULL))
  {
    snprintf(error_buffer, sizeof(error_buffer),"error: %d '%s'",
            io->error_code(), io->error_str());
    retval= ER_QUERY_ON_FOREIGN_DATA_SOURCE;
    goto error;
  }
  if (!(*result= io->store_result()))
  {
    retval= HA_ERR_END_OF_FILE;
    goto error;
  }
  if (!(retval= read_next(buf, *result)))
    DBUG_RETURN(retval);

  insert_dynamic(&results, (uchar*) result);
  *result= 0;
  DBUG_RETURN(retval);

error:
  my_error(retval, MYF(0), error_buffer);
  DBUG_RETURN(retval);
}


/*
  This method is used exlusevely by filesort() to check if we
  can create sorting buffers of necessary size.
  If the handler returns more records that it declares
  here server can just crash on filesort().
  We cannot guarantee that's not going to happen with
  the FEDERATEDX engine, as we have records==0 always if the
  client is a VIEW, and for the table the number of
  records can inpredictably change during execution.
  So we return maximum possible value here.
*/

ha_rows ha_federatedx::estimate_rows_upper_bound()
{
  return HA_POS_ERROR;
}


/* Initialized at each key walk (called multiple times unlike rnd_init()) */

int ha_federatedx::index_init(uint keynr, bool sorted)
{
  DBUG_ENTER("ha_federatedx::index_init");
  DBUG_PRINT("info", ("table: '%s'  key: %u", table->s->table_name.str, keynr));
  active_index= keynr;
  DBUG_RETURN(0);
}


/*
  Read first range
*/

int ha_federatedx::read_range_first(const key_range *start_key,
                                   const key_range *end_key,
                                   bool eq_range_arg, bool sorted)
{
  char sql_query_buffer[FEDERATEDX_QUERY_BUFFER_SIZE];
  int retval;
  String sql_query(sql_query_buffer,
                   sizeof(sql_query_buffer),
                   &my_charset_bin);
  DBUG_ENTER("ha_federatedx::read_range_first");

  DBUG_ASSERT(!(start_key == NULL && end_key == NULL));

  sql_query.length(0);
  //sql_query.append(share->select_query);
  append_select_from(sql_query);
  create_where_from_key(&sql_query,
                        &table->key_info[active_index],
                        start_key, end_key, 0, eq_range_arg);

  bool force_oltp = false;
  if (is_delete_update_target || statement_lock_type >= TL_READ_WITH_SHARED_LOCKS) {
    if (ha_thd()->lex->sql_command == SQLCOM_DELETE && !partial_ppd
        && ha_thd()->variables.fedx_vitess_push_limit_for_simple_dml) {
      ha_rows expand_factor = ha_thd()->variables.fedx_vitess_limit_expand_factor;
      ha_rows limit = 0;
      if (ha_thd()->lex && ha_thd()->lex->current_select) {
        limit = ha_thd()->lex->current_select->get_limit();
      }
      if (limit) {
        limit = limit * expand_factor;
        sql_query.append(STRING_WITH_LEN(" LIMIT "));
        sql_query.append_ulonglong(limit);
      }
    }
    if (statement_lock_type >= TL_WRITE_DELAYED)
        sql_query.append(STRING_WITH_LEN(" FOR UPDATE"));
    else
        sql_query.append(STRING_WITH_LEN(" LOCK IN SHARE MODE"));
    force_oltp = true;
  }
  if (!ha_thd()->transaction.all.is_empty()) {
    force_oltp = true;
  }

  if ((retval= txn->acquire(share, ha_thd(), TRUE, &io)))
    DBUG_RETURN(retval);

  if (stored_result)
    (void) free_result();

  if (io->query(sql_query.ptr(), sql_query.length(), force_oltp ? SCAN_MODE_OLTP : scan_mode, NULL))
  {
    retval= ER_QUERY_ON_FOREIGN_DATA_SOURCE;
    goto error;
  }
  sql_query.length(0);

  if (!(stored_result= io->store_result()))
  {
    retval= HA_ERR_END_OF_FILE;
    goto error;
  }

  retval= read_next(table->record[0], stored_result);
  DBUG_RETURN(retval);

error:
  DBUG_RETURN(retval);
}


int ha_federatedx::read_range_next()
{
  int retval;
  DBUG_ENTER("ha_federatedx::read_range_next");
  retval= rnd_next(table->record[0]);
  DBUG_RETURN(retval);
}


/* Used to read forward through the index.  */
int ha_federatedx::index_next(uchar *buf)
{
  DBUG_ENTER("ha_federatedx::index_next");
  int retval=read_next(buf, stored_result);
  DBUG_RETURN(retval);
}


void ha_federatedx::check_partial_read() {
  if (!(optimizer_flag(ha_thd(), OPTIMIZER_SWITCH_FEDX_RANGE_READ) || optimizer_flag(ha_thd(), OPTIMIZER_SWITCH_FEDX_SHARDED_READ))) {
    pr_info.partial_read_mode = PARTIAL_READ_NONE;
    return;
  }
  bool want_shard_read = false;
  bool want_range_read = false;

  // sql hint has highest priority
  ha_rows limit_num = HA_POS_ERROR;
  bool auto_partial_on_limit = optimizer_flag(ha_thd(), OPTIMIZER_SWITCH_FEDX_AUTO_PARTIAL_READ_ON_LIMIT);
  ha_rows partial_read_type = ha_thd()->variables.vitess_partial_read_type;
  if (auto_partial_on_limit) {
    //todo must find a better way to get the limit info
    if (ha_thd()->lex && ha_thd()->lex->current_select && ha_thd()->lex->current_select->join) {
      limit_num = ha_thd()->lex->current_select->join->row_limit;
    }
    if (limit_num == HA_POS_ERROR) {
      //todo we should use the first select_lex directly???
      if (ha_thd()->lex && ha_thd()->lex->select_lex.join) {
        limit_num = ha_thd()->lex->select_lex.join->row_limit;
      }
    }
    if (limit_num != HA_POS_ERROR && ha_thd()->lex
        && ha_thd()->lex->current_select && ha_thd()->lex->current_select->join
        && ha_thd()->lex->current_select->join->table_count > 1) {
      limit_num = limit_num * ha_thd()->variables.join_limit_scale;
    }
  }
  if (partial_read_mode_by_hint == PARTIAL_READ_SHARD_READ) {
    want_shard_read = true;
  } else if (partial_read_mode_by_hint == PARTIAL_READ_RANGE_READ) {
    want_range_read = true;
  } else if (partial_read_mode_by_hint == PARTIAL_READ_SHARD_RANGE_READ) {
    want_shard_read = want_range_read = true;
  } else if (partial_read_mode_by_hint == PARTIAL_READ_NONE) {
    want_shard_read = want_range_read = false;
  } else {
    if (table->s->comment.length > 0 && strstr(table->s->comment.str, "force partial read")) {
      want_shard_read = want_range_read = true;
    } else {
      ha_rows max_row = ha_thd()->variables.max_vitess_complete_read_size;
      if (max_row < stats.records || (auto_partial_on_limit &&
                                      limit_num != HA_POS_ERROR && limit_num < stats.records)) {
        if (partial_read_type >= 3 || (additionalFilter.length > 0 && has_equal_filter)) {
          // user does not allow partial read explicitly or has equal filter condition
          want_shard_read = want_range_read = false;
        } else {
          want_shard_read = want_range_read = true;
        }
      } else {
        // do not need partial read
        want_shard_read = want_range_read = false;
      }
    }
  }

  my_ulonglong  part_value_num = local_part_col_name == NULL ? share->part_value_num : local_part_value_num;
  const char *part_col_name = local_part_col_name == NULL ? share->part_col_name : local_part_col_name;
  if (part_col == NULL && part_value_num > 0 && part_col_name != NULL) {
    for (Field **field = table->field; *field; field++) {
      if (!strcasecmp(part_col_name, (*field)->field_name.str)) {
        //todo check the column type, types like blob should not be supported.
        part_col = *field;
        break;
      }
    }
  }

  bool support_range_read = part_col != NULL && part_value_num > 0 &&
                            optimizer_flag(ha_thd(), OPTIMIZER_SWITCH_FEDX_RANGE_READ);
  bool support_shard_read = share->s->shard_num > 1 && share->s->shard_num != 10000 &&
                            optimizer_flag(ha_thd(), OPTIMIZER_SWITCH_FEDX_SHARDED_READ);

  want_shard_read = want_shard_read && support_shard_read;
  want_range_read = want_range_read && support_range_read;

  if (want_range_read && want_shard_read) {
    if (partial_read_type == 0) {
      //range read preferred
      pr_info.partial_read_mode = PARTIAL_READ_RANGE_READ;
    } else if (partial_read_type == 1) {
      //shard read preferred
      pr_info.partial_read_mode = PARTIAL_READ_SHARD_READ;
    } else if (partial_read_type == 2) {
      // todo support partial range shard read
      //pr_info.partial_read_mode = PARTIAL_READ_RANGE_READ;
      pr_info.partial_read_mode = PARTIAL_READ_SHARD_RANGE_READ;
    } else {
      pr_info.partial_read_mode = PARTIAL_READ_NONE;
    }
  } else if (want_range_read) {
    pr_info.partial_read_mode = PARTIAL_READ_RANGE_READ;
  } else if (want_shard_read) {
    pr_info.partial_read_mode = PARTIAL_READ_SHARD_READ;
  } else {
    pr_info.partial_read_mode = PARTIAL_READ_NONE;
  }
}
/*
  rnd_init() is called when the system wants the storage engine to do a table
  scan.

  This is the method that gets data for the SELECT calls.

  See the federatedx in the introduction at the top of this file to see when
  rnd_init() is called.

  Called from filesort.cc, records.cc, sql_handler.cc, sql_select.cc,
  sql_table.cc, and sql_update.cc.
*/

int ha_federatedx::rnd_init(bool scan)
{
  DBUG_ENTER("ha_federatedx::rnd_init");
  /*
    The use of the 'scan' flag is incredibly important for this handler
    to work properly, especially with updates containing WHERE clauses
    using indexed columns.

    When the initial query contains a WHERE clause of the query using an
    indexed column, it's index_read_idx that selects the exact record from
    the foreign database.

    When there is NO index in the query, either due to not having a WHERE
    clause, or the WHERE clause is using columns that are not indexed, a
    'full table scan' done by rnd_init, which in this situation simply means
    a 'select * from ...' on the foreign table.

    In other words, this 'scan' flag gives us the means to ensure that if
    there is an index involved in the query, we want index_read_idx to
    retrieve the exact record (scan flag is 0), and do not  want rnd_init
    to do a 'full table scan' and wipe out that result set.

    Prior to using this flag, the problem was most apparent with updates.

    An initial query like 'UPDATE tablename SET anything = whatever WHERE
    indexedcol = someval', index_read_idx would get called, using a query
    constructed with a WHERE clause built from the values of index ('indexcol'
    in this case, having a value of 'someval').  mysql_store_result would
    then get called (this would be the result set we want to use).

    After this rnd_init (from sql_update.cc) would be called, it would then
    unecessarily call "select * from table" on the foreign table, then call
    mysql_store_result, which would wipe out the correct previous result set
    from the previous call of index_read_idx's that had the result set
    containing the correct record, hence update the wrong row!

  */

  if (scan)
  {
    int error;

    if ((error= txn->acquire(share, ha_thd(), TRUE, &io)))
      DBUG_RETURN(error);

    if (stored_result)
      (void) free_result();

    check_partial_read();
    if (pr_info.partial_read_mode != PARTIAL_READ_NONE) {
      pr_info.shard_names = share->s->shard_names;
      pr_info.shard_num = share->s->shard_num;
      pr_info.sharded_offset = 0;

      if (local_part_col_name) {
        pr_info.range_num = local_part_value_num;
        pr_info.range_values = local_part_values;
      } else {
        pr_info.range_num = share->part_value_num;
        pr_info.range_values = share->part_values;
      }
      pr_info.range_offset = 0;

      pr_info.part_col = part_col;
    }

    char sql_query_buffer[FEDERATEDX_QUERY_BUFFER_SIZE];
    String sql_query(sql_query_buffer,
                     sizeof(sql_query_buffer),
                     &my_charset_bin);
    sql_query.length(0);
    //sql_query.append(share->select_query, strlen(share->select_query));
    append_select_from(sql_query);

    if (pr_info.partial_read_mode != PARTIAL_READ_NONE) {
      dynstr_trunc(&pr_info.partial_read_query, pr_info.partial_read_query.length);
      dynstr_append_mem(&pr_info.partial_read_query, sql_query.ptr(), sql_query.length());

      dynstr_trunc(&pr_info.partial_read_filter, pr_info.partial_read_filter.length);
      dynstr_append_mem(&pr_info.partial_read_filter, additionalFilter.str, additionalFilter.length);

      pr_info.need_for_update = false;
    }

    if (additionalFilter.length > 0) {
      sql_query.append(STRING_WITH_LEN(" WHERE "));
      sql_query.append(additionalFilter.str, additionalFilter.length);
    } else if (table_share->sequence && table_share->sequence->remote) {
      sql_query.append(STRING_WITH_LEN(" WHERE _kundb_dummy_next_val = 1 and _kundb_dummy_seq_name = "));
      append_ident(&sql_query, share->table_name,
                   share->table_name_length, value_quote_char);
    }

    bool force_oltp = false;
    if (is_delete_update_target || statement_lock_type >= TL_READ_WITH_SHARED_LOCKS) {
      if (ha_thd()->lex->sql_command == SQLCOM_DELETE && !partial_ppd
          && ha_thd()->variables.fedx_vitess_push_limit_for_simple_dml) {
        ha_rows expand_factor = ha_thd()->variables.fedx_vitess_limit_expand_factor;
        ha_rows limit = 0;
        if (ha_thd()->lex && ha_thd()->lex->current_select) {
          limit = ha_thd()->lex->current_select->get_limit();
        }
        if (limit) {
          limit = limit * expand_factor;
          sql_query.append(STRING_WITH_LEN(" LIMIT "));
          sql_query.append_ulonglong(limit);
        }
      }
      if (is_delete_update_target || statement_lock_type >= TL_WRITE_DELAYED) {
          sql_query.append(STRING_WITH_LEN(" FOR UPDATE"));
      } else {
          sql_query.append(STRING_WITH_LEN(" LOCK IN SHARE MODE"));
      }

      force_oltp = true;
      if (pr_info.partial_read_mode != PARTIAL_READ_NONE) {
        pr_info.need_for_update = true;
      }
    }
    if (!ha_thd()->transaction.all.is_empty()) {
      force_oltp = true;
    }

    if (pr_info.partial_read_mode != PARTIAL_READ_NONE) {
      // for partial read, initialize pr_info
      partial_read_scan_mode = force_oltp ? SCAN_MODE_OLTP : scan_mode;
    }

    if (io->query(sql_query.ptr(), strlen(sql_query.ptr()), force_oltp ? SCAN_MODE_OLTP : scan_mode,
                  pr_info.partial_read_mode != PARTIAL_READ_NONE ? &pr_info : NULL))
      goto error;

    stored_result= io->store_result();
    if (!stored_result)
      goto error;
  }
  DBUG_RETURN(0);

error:
  DBUG_RETURN(stash_remote_error());
}

bool is_bit_set(MY_BITMAP *def_set, MY_BITMAP *tmp_set, MY_BITMAP *active_set, uint bit) {
  if (def_set == active_set || tmp_set == active_set) {
    // looks like the condition is always true, but do not have enough confidence :(
    return bitmap_is_set(active_set, bit) || (def_set != NULL && bitmap_is_set(def_set, bit))
           || (tmp_set != NULL && bitmap_is_set(tmp_set, bit));
  }
  return bitmap_is_set(active_set, bit);
}

// zqdai add support for column pruning
void ha_federatedx::append_select_from(String& sql_query)
{
    THD * thd = ha_thd();
    bool skip_cp = false;
    if (thd) {
      bool is_dml = false;
      switch (thd->lex->sql_command) {
        case SQLCOM_DELETE:
        case SQLCOM_DELETE_MULTI:
        case SQLCOM_UPDATE:
        case SQLCOM_UPDATE_MULTI:
          is_dml = true;
              break;
        default:
          is_dml = false;
      }

      if (is_dml && !optimizer_flag(thd, OPTIMIZER_SWITCH_FEDX_CP_DML)) {
        skip_cp = true;
      }
      if (!is_dml && !optimizer_flag(thd, OPTIMIZER_SWITCH_FEDX_CP_QUERY)) {
        skip_cp = true;
      }
    }

    sql_query.set_charset(system_charset_info);
    sql_query.append(STRING_WITH_LEN("SELECT "));
    MY_BITMAP *def_set = table->def_read_set.bitmap ? &table->def_read_set : NULL;
    MY_BITMAP *tmp_set = table->tmp_set.bitmap ? &table->tmp_set : NULL;
    for (Field **field = table->field; *field; field++) {
        if (skip_cp || is_bit_set(def_set, tmp_set, table->read_set,(*field)->field_index)) {
            append_ident(&sql_query, (*field)->field_name.str,
                         (*field)->field_name.length, ident_quote_char);
            sql_query.append(STRING_WITH_LEN(", "));
        } else {
            sql_query.append(STRING_WITH_LEN(" NULL AS "));
            append_ident(&sql_query, (*field)->field_name.str,
                         (*field)->field_name.length, ident_quote_char);
            sql_query.append(STRING_WITH_LEN(", "));
        }
    }
    /* chops off trailing comma */
    sql_query.length(sql_query.length() - sizeof_trailing_comma);

    sql_query.append(STRING_WITH_LEN(" FROM "));

    append_ident(&sql_query, share->table_name,
                 share->table_name_length, ident_quote_char);
}

int ha_federatedx::rnd_end()
{
  DBUG_ENTER("ha_federatedx::rnd_end");
  DBUG_RETURN(index_end());
}

ha_rows ha_federatedx::multi_range_read_info_const(uint keyno, RANGE_SEQ_IF *seq,
                                     void *seq_init_param, uint n_ranges_arg,
                                     uint *bufsz, uint *flags, Cost_estimate *cost)
{
  ha_rows rows =
          handler::multi_range_read_info_const(
                  keyno,
                  seq,
                  seq_init_param,
                  n_ranges_arg,
                  bufsz,
                  flags,
                  cost
          );
  if (*flags & HA_MRR_FEDX_MRR && !(*flags & HA_MRR_SORTED)) {
    *flags &= ~HA_MRR_USE_DEFAULT_IMPL;
    *flags |= HA_MRR_NO_ASSOCIATION;
    use_default_mrr = false;
  } else {
    *flags |= HA_MRR_USE_DEFAULT_IMPL;
    *flags &= ~HA_MRR_SORTED;
    use_default_mrr = true;
    //if (*flags & HA_MRR_SORTED) {
    //  rows = HA_POS_ERROR;
    //}
  }
  DBUG_PRINT("info",("federatedx rows=%llu", rows));
  return rows;
}

int
ha_federatedx::multi_range_read_init(RANGE_SEQ_IF *seq_funcs, void *seq_init_param,
                               uint n_ranges, uint mode, HANDLER_BUFFER *buf)
{
  if (!(mode & HA_MRR_FEDX_MRR)) {
    use_default_mrr = true;
  }
  return handler::multi_range_read_init(seq_funcs, seq_init_param, n_ranges, mode, buf);
}

ha_rows ha_federatedx::multi_range_read_info(uint keyno, uint n_ranges, uint n_rows,
                              uint key_parts, uint *bufsz,
                              uint *flags, Cost_estimate *cost) {
  ha_rows rows = handler::multi_range_read_info(keyno, n_ranges, n_rows, key_parts, bufsz, flags, cost);
  if (*flags & HA_MRR_FEDX_MRR && !(*flags & HA_MRR_SORTED)) {
    *flags &= ~HA_MRR_USE_DEFAULT_IMPL;
    *flags |= HA_MRR_NO_ASSOCIATION;
    use_default_mrr = false;
  } else {
    *flags |= HA_MRR_USE_DEFAULT_IMPL;
    *flags &= ~HA_MRR_SORTED;
    use_default_mrr = true;
    //if (*flags & HA_MRR_SORTED) {
    //  //federatedx does not support mrr_sorted
    //  rows = HA_POS_ERROR;
    //}
  }
  DBUG_PRINT("info",("federatedx rows=%llu", rows));
  return rows;
}

int ha_federatedx::read_multi_in_first(String *in_filter_str)
{
  char sql_query_buffer[FEDERATEDX_QUERY_BUFFER_SIZE];
  int retval;
  String sql_query(sql_query_buffer,
                   sizeof(sql_query_buffer),
                   &my_charset_bin);
  DBUG_ENTER("ha_federatedx::read_multi_in_first");

  sql_query.length(0);
  append_select_from(sql_query);
  sql_query.append(STRING_WITH_LEN(" WHERE "));
  sql_query.append(in_filter_str->ptr(), in_filter_str->length());
  if (additionalFilter.length != 0) {
      if (sql_query.append(STRING_WITH_LEN(" AND ("))) {
          dynstr_trunc(&additionalFilter, additionalFilter.length);
          DBUG_RETURN(ER_ENGINE_OUT_OF_MEMORY);
      }
      if (sql_query.append(additionalFilter.str, additionalFilter.length)) {
          dynstr_trunc(&additionalFilter, additionalFilter.length);
          DBUG_RETURN(ER_ENGINE_OUT_OF_MEMORY);
      }
      if (sql_query.append(STRING_WITH_LEN(")"))) {
          dynstr_trunc(&additionalFilter, additionalFilter.length);
          DBUG_RETURN(ER_ENGINE_OUT_OF_MEMORY);
      }
  }

  bool force_oltp = false;
  if (is_delete_update_target || statement_lock_type >= TL_READ_WITH_SHARED_LOCKS) {
    if (statement_lock_type >= TL_WRITE_DELAYED)
        sql_query.append(STRING_WITH_LEN(" FOR UPDATE"));
    else
        sql_query.append(STRING_WITH_LEN(" LOCK IN SHARE MODE"));
    force_oltp = true;
  }
  if (!ha_thd()->transaction.all.is_empty()) {
    force_oltp = true;
  }

  if ((retval= txn->acquire(share, ha_thd(), TRUE, &io)))
    DBUG_RETURN(retval);

  if (stored_result)
    (void) free_result();

  if (io->query(sql_query.ptr(), sql_query.length(), force_oltp ? SCAN_MODE_OLTP : scan_mode, NULL))
  {
    retval= ER_QUERY_ON_FOREIGN_DATA_SOURCE;
    goto error;
  }
  sql_query.length(0);

  if (!(stored_result= io->store_result()))
  {
    retval= HA_ERR_END_OF_FILE;
    goto error;
  }

  retval= read_next(table->record[0], stored_result);
  DBUG_RETURN(retval);

error:
  DBUG_RETURN(retval);
}

int ha_federatedx::multi_range_read_next(range_id_t *range_info)
{
  if (use_default_mrr) {
    return handler::multi_range_read_next(range_info);
  }
  int result= HA_ERR_END_OF_FILE;
  bool range_res;
  DBUG_ENTER("ha_federatedx::multi_range_read_next");

  if (!mrr_have_range)
  {
    mrr_have_range= TRUE;
    goto start;
  }

  do
  {
    result= read_range_next();
    /* On success or non-EOF errors jump to the end. */
    if (result != HA_ERR_END_OF_FILE)
      break;

start:
    char tmpbuff[FEDERATEDX_QUERY_BUFFER_SIZE];
    char tmpbuff1[FEDERATEDX_QUERY_BUFFER_SIZE];
    char tmpbuff2[FEDERATEDX_QUERY_BUFFER_SIZE];
    String in_filter_str(tmpbuff, sizeof(tmpbuff), system_charset_info);
    String column_names(tmpbuff1, sizeof(tmpbuff), system_charset_info);
    String in_values(tmpbuff2, sizeof(tmpbuff), system_charset_info);
    in_filter_str.length(0);
    column_names.length(0);
    in_values.length(0);
    KEY *key_info = &(table->key_info[active_index]);
    bool need_query = false;
    bool need_construct_column_names = true;
    my_ulonglong max_in_size = ha_thd() ? ha_thd()->variables.fedx_bkah_size : FEDERATEDX_MAX_IN_SIZE;

    for (uint i = 0;i < max_in_size; i++) {
      if (!(range_res = mrr_funcs.next(mrr_iter, &mrr_cur_range))) {
        if (i > 0) {
          in_values.append(STRING_WITH_LEN(", "));
          if (need_construct_column_names) {
            column_names.append(STRING_WITH_LEN(", "));
          }
        } else {
          in_values.append(STRING_WITH_LEN("("));
          if (need_construct_column_names) {
            column_names.append(STRING_WITH_LEN("("));
          }
        }

        bool needs_quotes;
        KEY_PART_INFO *key_part;
        key_range *key = &(mrr_cur_range.start_key);
        uint remainder, length, value_index;
        const uchar *ptr;

        in_values.append(STRING_WITH_LEN("("));
        for (key_part = key_info->key_part,
                remainder = key_info->user_defined_key_parts,
                length = key->length,
                ptr = key->key,
                value_index = 0; ;
                remainder--, key_part++, value_index++) {

          if (value_index != 0) {
            in_values.append(STRING_WITH_LEN(", "));
            if (need_construct_column_names) {
              column_names.append(STRING_WITH_LEN(", "));
            }
          }

          Field *field = key_part->field;
          uint store_length = key_part->store_length;
          uint part_length= MY_MIN(store_length, length);
          needs_quotes = field->str_needs_quotes();

          if (need_construct_column_names) {
            emit_key_part_name(&column_names, key_part);
          }
          if (key_part->null_bit) {
            if (*ptr++) {
              // todo the key is null, should not reach here
              in_values.append(STRING_WITH_LEN("NULL"));
              goto prepare_for_next_key_part;
            }
          }

          emit_key_part_element(&in_values, key_part, needs_quotes, 0, ptr, part_length);

prepare_for_next_key_part:
          if (store_length >= length) {
            break;
          }
          DBUG_PRINT("info", ("remainder %d", remainder));
          DBUG_ASSERT(remainder > 1);
          length-= store_length;
          ptr += store_length - MY_TEST(key_part->null_bit);
        }
        in_values.append(STRING_WITH_LEN(")"));

        need_query = true;
        need_construct_column_names = false;

      } else {
        // no more key
        break;
      }

      if (max_query_size > 0 && in_values.length() > max_query_size * 0.9) {
        break;
      }
    }

    if (need_query) {
      column_names.append(STRING_WITH_LEN(")"));
      in_values.append(STRING_WITH_LEN(")"));
      in_filter_str.append(column_names.ptr(), column_names.length());
      in_filter_str.append(STRING_WITH_LEN(" in "));
      in_filter_str.append(in_values.ptr(), in_values.length());
      result = read_multi_in_first(&in_filter_str);
      if (result != HA_ERR_END_OF_FILE)
        break;
    }

  }
  while ((result == HA_ERR_END_OF_FILE) && !range_res);

  if (result == 1) {
    result = ER_UNKNOWN_ERROR;
  }
  if (result && result != HA_ERR_END_OF_FILE) {
    set_err_status(result);
  }
  DBUG_PRINT("exit",("ha_federatedx::multi_range_read_next result %d", result));
  DBUG_RETURN(result);
}

void ha_federatedx::set_err_status(int err) {
  DBUG_ENTER("ha_federatedx::set_err_status");
  if (!err)
    return;

  THD* thd = ha_thd();
  DBUG_ASSERT(thd);

  Diagnostics_area* da = thd->get_stmt_da();
  DBUG_ASSERT(da);

  da->set_overwrite_status(true);
  da->set_error_status(err);
  DBUG_VOID_RETURN;
}

int ha_federatedx::free_result()
{
  int error;
  DBUG_ENTER("ha_federatedx::free_result");
  DBUG_ASSERT(stored_result);
  for (uint i= 0; i < results.elements; ++i)
  {
    FEDERATEDX_IO_RESULT *result= 0;
    get_dynamic(&results, (uchar*) &result, i);
    if (result == stored_result)
      goto end;
  }
  if (position_called)
  {
    insert_dynamic(&results, (uchar*) &stored_result);
  }
  else
  {
    federatedx_io *tmp_io= 0, **iop;
    if (!*(iop= &io) && (error= txn->acquire(share, ha_thd(), TRUE, (iop= &tmp_io))))
    {
      DBUG_ASSERT(0);                             // Fail when testing
      insert_dynamic(&results, (uchar*) &stored_result);
      goto end;
    }
    (*iop)->free_result(stored_result);
    txn->release(&tmp_io);
  }
end:
  stored_result= 0;
  position_called= FALSE;
  DBUG_RETURN(0);
}

int ha_federatedx::index_end(void)
{
  int error= 0;
  DBUG_ENTER("ha_federatedx::index_end");
  if (stored_result)
    error= free_result();
  active_index= MAX_KEY;
  DBUG_RETURN(error);
}

bool partial_read_has_next(partial_read_info pr_info, int partial_read_mode) {
  if (partial_read_mode == PARTIAL_READ_RANGE_READ) {
    return  pr_info.range_offset <= pr_info.range_num;
  } else if (partial_read_mode == PARTIAL_READ_SHARD_READ) {
    return pr_info.sharded_offset < pr_info.shard_num;
  } else if (partial_read_mode == PARTIAL_READ_SHARD_RANGE_READ) {
    return !(pr_info.range_offset > pr_info.range_num
             && pr_info.sharded_offset == pr_info.shard_num -1);
  } else {
    return false;
  }
}

/*
  This is called for each row of the table scan. When you run out of records
  you should return HA_ERR_END_OF_FILE. Fill buff up with the row information.
  The Field structure for the table is the key to getting data into buf
  in a manner that will allow the server to understand it.

  Called from filesort.cc, records.cc, sql_handler.cc, sql_select.cc,
  sql_table.cc, and sql_update.cc.
*/

int ha_federatedx::rnd_next(uchar *buf)
{
  DBUG_ENTER("ha_federatedx::rnd_next");

  if (stored_result == 0)
  {
    /*
      Return value of rnd_init is not always checked (see records.cc),
      so we can get here _even_ if there is _no_ pre-fetched result-set!
      TODO: fix it. We can delete this in 5.1 when rnd_init() is checked.
    */
    DBUG_RETURN(1);
  }
  int retval=read_next(buf, stored_result);
  while (retval == HA_ERR_END_OF_FILE && pr_info.partial_read_mode != PARTIAL_READ_NONE
         && partial_read_has_next(pr_info, pr_info.partial_read_mode)) {
    // in partial read mode, we need read next part if possible
    free_result();
    int error;
    if ((error= txn->acquire(share, ha_thd(), TRUE, &io)))
      DBUG_RETURN(error);
    if (io->query("aaaaa", 5, partial_read_scan_mode, &pr_info))
      goto err;

    stored_result= io->store_result();
    if (!stored_result)
      goto err;
    retval=read_next(buf, stored_result);
  }
  DBUG_RETURN(retval);
err:
  DBUG_RETURN(stash_remote_error());
}


/*
  ha_federatedx::read_next

  reads from a result set and converts to mysql internal
  format

  SYNOPSIS
    field_in_record_is_null()
      buf       byte pointer to record
      result    mysql result set

    DESCRIPTION
     This method is a wrapper method that reads one record from a result
     set and converts it to the internal table format

    RETURN VALUE
      1    error
      0    no error 
*/

int ha_federatedx::read_next(uchar *buf, FEDERATEDX_IO_RESULT *result)
{
  int retval;
  FEDERATEDX_IO_ROW *row;
  DBUG_ENTER("ha_federatedx::read_next");

  if ((retval= txn->acquire(share, ha_thd(), TRUE, &io)))
    DBUG_RETURN(retval);

  /* Fetch a row, insert it back in a row format. */
  if (!(row= io->fetch_row(result, &current)))
    DBUG_RETURN(HA_ERR_END_OF_FILE);

  if (!(retval= convert_row_to_internal_format(buf, row, result)))
    table->status= 0;

  DBUG_RETURN(retval);
}


/**
  @brief      Store a reference to current row.

  @details    During a query execution we may have different result sets (RS),
              e.g. for different ranges. All the RS's used are stored in
              memory and placed in @c results dynamic array. At the end of
              execution all stored RS's are freed at once in the
              @c ha_federated::reset().
              So, in case of federated, a reference to current row is a
              stored result address and current data cursor position.
              As we keep all RS in memory during a query execution,
              we can get any record using the reference any time until
              @c ha_federated::reset() is called.
              TODO: we don't have to store all RS's rows but only those
              we call @c ha_federated::position() for, so we can free memory
              where we store other rows in the @c ha_federated::index_end().

  @param[in]  record  record data (unused)

*/

void ha_federatedx::position(const uchar *record __attribute__ ((unused)))
{
  DBUG_ENTER("ha_federatedx::position");

  if (!stored_result)
  {
    bzero(ref, ref_length);
    DBUG_VOID_RETURN;
  }

  if (txn->acquire(share, ha_thd(), TRUE, &io))
    DBUG_VOID_RETURN;

  io->mark_position(stored_result, ref, current);

  position_called= TRUE;

  DBUG_VOID_RETURN;
}


/*
  This is like rnd_next, but you are given a position to use to determine the
  row. The position will be of the type that you stored in ref.

  This method is required for an ORDER BY

  Called from filesort.cc records.cc sql_insert.cc sql_select.cc sql_update.cc.
*/

int ha_federatedx::rnd_pos(uchar *buf, uchar *pos)
{
  int retval;
  FEDERATEDX_IO_RESULT *result= stored_result;
  DBUG_ENTER("ha_federatedx::rnd_pos");

  /* We have to move this to 'ref' to get things aligned */
  bmove(ref, pos, ref_length);

  if ((retval= txn->acquire(share, ha_thd(), TRUE, &io)))
    goto error;

  if ((retval= io->seek_position(&result, ref)))
    goto error;

  retval= read_next(buf, result);
  DBUG_RETURN(retval);

error:
  DBUG_RETURN(retval);
}


/*
  ::info() is used to return information to the optimizer.
  Currently this table handler doesn't implement most of the fields
  really needed. SHOW also makes use of this data
  Another note, you will probably want to have the following in your
  code:
  if (records < 2)
    records = 2;
  The reason is that the server will optimize for cases of only a single
  record. If in a table scan you don't know the number of records
  it will probably be better to set records to two so you can return
  as many records as you need.
  Along with records a few more variables you may wish to set are:
    records
    deleted
    data_file_length
    index_file_length
    delete_length
    check_time
  Take a look at the public variables in handler.h for more information.

  Called in:
    filesort.cc
    ha_heap.cc
    item_sum.cc
    opt_sum.cc
    sql_delete.cc
    sql_delete.cc
    sql_derived.cc
    sql_select.cc
    sql_select.cc
    sql_select.cc
    sql_select.cc
    sql_select.cc
    sql_show.cc
    sql_show.cc
    sql_show.cc
    sql_show.cc
    sql_table.cc
    sql_union.cc
    sql_update.cc

*/


double ha_federatedx::scan_time()
{
  DBUG_PRINT("info", ("records %lu", (ulong) stats.records));
  if (ha_thd() && optimizer_flag(ha_thd(), OPTIMIZER_SWITCH_FEDX_CBO_WITH_ACTUAL_RECORDS)) {
    ha_rows scan_expand_factor = ha_thd()->variables.fedx_scan_expand_factor;
    ha_rows blob_scan_penalty_factor = 1;
    if (field_type == FIELD_HAS_BLOB &&
            stats.records > ha_thd()->variables.fedx_blob_scan_equal_ref_threshold) {
      blob_scan_penalty_factor = ha_thd()->variables.fedx_blob_scan_penalty_factor;
    }
    return (double)(stats.records * scan_expand_factor * blob_scan_penalty_factor);
  }
  return (double)(stats.records*1000);
}

double ha_federatedx::read_time(uint index, uint ranges, ha_rows rows)
{
  /*
    Per Brian, this number is bugus, but this method must be implemented,
    and at a later date, he intends to document this issue for handler code
  */
  if (ha_thd() && optimizer_flag(ha_thd(), OPTIMIZER_SWITCH_FEDX_CBO_WITH_ACTUAL_RECORDS)) {
    if (is_valid_index(index)) {
      ha_rows blob_scan_penalty_factor = 1;
      if (field_type == FIELD_HAS_BLOB &&
              rows > ha_thd()->variables.fedx_blob_scan_equal_ref_threshold) {
        blob_scan_penalty_factor = ha_thd()->variables.fedx_blob_scan_penalty_factor;
      }
      return (double) rows*blob_scan_penalty_factor + 1;
    } else {
      ha_rows invalid_index_expand_factor = ha_thd()->variables.fedx_invalid_index_expand_factor;
      double ret = stats.records * invalid_index_expand_factor > rows ? stats.records * invalid_index_expand_factor : rows;
      ret = ret * ha_thd()->variables.fedx_scan_expand_factor + 1;
      return ret;
    }
  }
  return (double) rows /  20.0+1;
}

uint ha_federatedx::init_shard_info(federatedx_io *io) {
  // initialize shard name infomation for vitess table
  DBUG_ENTER("ha_federatedx::init_shard_info");
  DYNAMIC_ARRAY shard_infos;
  uint error_code = 0;
  my_init_dynamic_array(&shard_infos, sizeof(char **), 4, 4, MYF(0));
  char shard_buffer[FEDERATEDX_QUERY_BUFFER_SIZE];
  String shard_query(shard_buffer, sizeof(shard_buffer), &my_charset_bin);
  shard_query.length(0);
  shard_query.append(STRING_WITH_LEN("SHOW KUNDB_SHARDS "));
  shard_query.append(STRING_WITH_LEN("`"));
  shard_query.append(share->s->database);
  shard_query.append(STRING_WITH_LEN("`"));
  if (io->query(shard_query.ptr(), shard_query.length(), SCAN_MODE_DEFAULT, NULL)) {
    mysql_mutex_lock(&share->s->mutex);
    if (share->s->shard_num == 0) {
      share->s->shard_num = 10000;
    }
    mysql_mutex_unlock(&share->s->mutex);
    DBUG_RETURN(0);
  } else {
    FEDERATEDX_IO_RESULT *shard_info_result = NULL;
    FEDERATEDX_IO_ROW *row;
    shard_info_result = io->store_result();
    if (shard_info_result == NULL) {
      mysql_mutex_lock(&share->s->mutex);
      if (share->s->shard_num == 0) {
        share->s->shard_num = 10000;
      }
      mysql_mutex_unlock(&share->s->mutex);
      DBUG_RETURN(0);
    } else {
      my_ulonglong rownum = io->get_num_rows(shard_info_result);
      for (my_ulonglong i = 0; i < rownum; i++) {
        if (!(row = io->fetch_row(shard_info_result, NULL))) {
          io->free_result(shard_info_result);
          error_code = ER_QUERY_ON_FOREIGN_DATA_SOURCE;
          DBUG_RETURN(error_code);
        }

        const char *shard_name = io->get_column_data(row, 0);
        insert_dynamic(&shard_infos, &shard_name);
      }

      if (shard_infos.elements == 0) {
        // do not found any valid shard name, must be something wrong
        // in the remote vitess server
        io->free_result(shard_info_result);
        delete_dynamic(&shard_infos);
        error_code = ER_QUERY_ON_FOREIGN_DATA_SOURCE;
        DBUG_RETURN(error_code);
      } else {
        // set the shard info into server
        mysql_mutex_lock(&share->s->mutex);
        if (share->s->shard_num == 0) {
          for (uint i = 0; i < shard_infos.elements; i++) {
            const char **shard_name = dynamic_element(&shard_infos, i, const char**);
            share->s->shard_names[i] = strdup_root(&share->s->mem_root, *shard_name);
          }
          share->s->shard_num = shard_infos.elements;
        }
        mysql_mutex_unlock(&share->s->mutex);
        io->free_result(shard_info_result);
        delete_dynamic(&shard_infos);
      }
    }
    DBUG_RETURN(0);
  }
}

uint ha_federatedx::init_global_range_info(federatedx_io *io) {
  DBUG_ENTER("ha_federatedx::init_global_range_info");
  uint error_code = 0;
  char range_info_query_buffer[FEDERATEDX_QUERY_BUFFER_SIZE];
  String query(range_info_query_buffer, sizeof(range_info_query_buffer), &my_charset_bin);
  query.length(0);
  query.append(STRING_WITH_LEN("SHOW KUNDB_RANGE_INFO "));
  query.append(STRING_WITH_LEN("`"));
  query.append(share->table_name, share->table_name_length);
  query.append(STRING_WITH_LEN("`"));
  FEDERATEDX_IO_RESULT *shard_info_result = NULL;
  FEDERATEDX_IO_ROW *row;
  if (io->query(query.ptr(), query.length(), SCAN_MODE_DEFAULT, NULL)) {
    mysql_mutex_lock(&share->s->mutex);
    if (share->part_col_name == 0) {
      share->part_col_name = strdup_root(&share->mem_root, "no_part_col");
      share->part_value_num = 0;
    }
    mysql_mutex_unlock(&share->s->mutex);
    DBUG_RETURN(0);
  } else {
    shard_info_result = io->store_result();
    if (shard_info_result == NULL) {
      mysql_mutex_lock(&share->s->mutex);
      if (share->part_col_name == 0) {
        share->part_col_name = strdup_root(&share->mem_root, "no_part_col");
        share->part_value_num = 0;
      }
      mysql_mutex_unlock(&share->s->mutex);
      DBUG_RETURN(0);
    } else {
      my_ulonglong rownum = io->get_num_rows(shard_info_result);
      if (rownum == 0) {
        // not range info
        mysql_mutex_lock(&share->s->mutex);
        if (share->part_col_name == 0) {
          share->part_col_name = strdup_root(&share->mem_root, "no_part_col");
          share->part_value_num = rownum;
        }
        mysql_mutex_unlock(&share->s->mutex);
      } else {
        const char *range_values[HA_FEDERATEDX_VITESS_MAX_PART_NUM];
        const char *part_col_name;
        for (my_ulonglong i = 0; i < rownum; i++) {
          if (!(row = io->fetch_row(shard_info_result, NULL))) {
            io->free_result(shard_info_result);
            error_code = ER_QUERY_ON_FOREIGN_DATA_SOURCE;
            DBUG_RETURN(error_code);
          }
          if (i == 0) {
            part_col_name = io->get_column_data(row, 0);
          }
          range_values[i] = io->get_column_data(row, 1);
        }
        // todo should use per share mutex
        mysql_mutex_lock(&share->s->mutex);
        if (share->part_col_name == 0) {
          share->part_col_name = strdup_root(&share->mem_root, part_col_name);
          for (my_ulonglong i = 0; i < rownum; i++) {
            share->part_values[i] = strdup_root(&share->mem_root, range_values[i]);
          }
          share->part_value_num = rownum;
        }
        mysql_mutex_unlock(&share->s->mutex);
      }
      io->free_result(shard_info_result);
      DBUG_RETURN(0);
    }
  }
}

uint ha_federatedx::init_local_range_info(federatedx_io *io) {
  DBUG_ENTER("ha_federatedx::init_local_range_info");
  uint error_code;
  char range_info_query_buffer[FEDERATEDX_QUERY_BUFFER_SIZE];
  String query(range_info_query_buffer, sizeof(range_info_query_buffer), &my_charset_bin);
  query.length(0);
  query.append(STRING_WITH_LEN("SHOW KUNDB_RANGE_INFO "));
  query.append(STRING_WITH_LEN("`"));
  query.append(share->table_name, share->table_name_length);
  query.append(STRING_WITH_LEN("`"));
  FEDERATEDX_IO_ROW *row;
  if (io->query(query.ptr(), query.length(), SCAN_MODE_DEFAULT, NULL)) {
    local_part_col_name = "no_part_col";
    local_part_value_num = 0;
  } else {
    local_shard_info_result = io->store_result();
    if (local_shard_info_result == NULL) {
      local_part_col_name = "no_part_col";
      local_part_value_num = 0;
    } else {
      my_ulonglong rownum = io->get_num_rows(local_shard_info_result);
      if (rownum == 0) {
        // not range info
        local_part_col_name = "no_part_col";
        local_part_value_num = 0;
      } else {
        for (my_ulonglong i = 0; i < rownum; i++) {
          if (!(row = io->fetch_row(local_shard_info_result, NULL))) {
            io->free_result(local_shard_info_result);
            local_part_col_name = "no_part_col";
            error_code = ER_QUERY_ON_FOREIGN_DATA_SOURCE;
            DBUG_RETURN(error_code);
          }
          if (i == 0) {
            local_part_col_name = io->get_column_data(row, 0);
          }
          local_part_values[i] = io->get_column_data(row, 1);
        }
        local_part_value_num = rownum;
      }
    }
  }
  DBUG_RETURN(0);
}

void ha_federatedx::mark_read_columns_needed_for_update_delete(MY_BITMAP *read_map, MY_BITMAP *write_map) {
  // set pk column field
  if (pk_num > 0 && ha_thd() && ha_thd()->variables.fedx_pk_update_delete_level) {
    ha_rows pk_level = ha_thd()->variables.fedx_pk_update_delete_level;
    if (pk_level == 2) {
      bitmap_union(read_map, &pk_set);
    } else if (pk_level == 1 && pk_num == 1) {
      bitmap_union(read_map, &pk_set);
    }
  }

  // set vindex column field
  if (vindex_init) {
    if (bitmap_is_overlapping(write_map, &vindex_set)) {
      //todo should fetch all needed column information in update_row(), that will be much more efficient
      bitmap_set_all(read_map);
    } else {
      // vindex column is not updated, so just vindex column is needed
      bitmap_union(read_map, &vindex_set);
    }
  }
}

uint ha_federatedx::init_vindex_info(federatedx_io *io) {
  uint error_code;
  DBUG_ENTER("ha_federatedx::init_vindex_info");
  char vindex_query_buffer[FEDERATEDX_QUERY_BUFFER_SIZE];
  String query(vindex_query_buffer, sizeof(vindex_query_buffer), &my_charset_bin);
  query.length(0);
  query.append(STRING_WITH_LEN("SHOW KUNDB_VINDEXES in "));
  query.append(STRING_WITH_LEN("`"));
  query.append(share->table_name, share->table_name_length);
  query.append(STRING_WITH_LEN("`"));
  FEDERATEDX_IO_ROW *row;
  if (io->query(query.ptr(), query.length(), SCAN_MODE_DEFAULT, NULL)) {
    goto error;
  } else {
    FEDERATEDX_IO_RESULT *vindex_result = io->store_result();
    if (vindex_result == NULL) {
        goto error;
    } else {
      my_ulonglong rownum = io->get_num_rows(vindex_result);
      if (rownum == 0) {
        // no vindex info
        vindex_init = true;
      } else {
        for (my_ulonglong i = 0; i < rownum; i++) {
          if (!(row = io->fetch_row(vindex_result, NULL))) {
            io->free_result(vindex_result);
            goto error;
          }
          const char* column_name = io->get_column_data(row, 0);
          for (Field **field = table->field; *field; field++) {
            if (!strcasecmp((*field)->field_name.str, column_name)) {
              bitmap_set_bit(&vindex_set, (*field)->field_index);
              break;
            }
          }
        }
      }
      vindex_init = true;
      io->free_result(vindex_result);
    }
  }
  DBUG_RETURN(0);
error:
  error_code = ER_QUERY_ON_FOREIGN_DATA_SOURCE;
  DBUG_RETURN(error_code);
}

int ha_federatedx::find_index_num(const char *key_name) {
  for (uint i = 0; i < table->s->keys; i++) {
    if (!strcmp(table->key_info[i].name.str, key_name)) {
      return i;
    }
  }
  return -1;
}

void ha_federatedx::init_rec_per_key() {
  KEY *key_info;
  for(uint key_index = 0; key_index < table->s->keys; key_index++) {
    key_info = &table->key_info[key_index];
    if (key_info->user_defined_key_parts == 1) {
      // only init rec_per_key for key contains 1 columns
      if (is_valid_index(key_index) && key_info->rec_per_key != NULL) {
        ha_rows rec_num = records_per_shard / index_cardinality[key_index];
        if (key_index != table->s->primary_key && !(key_info->flags & HA_NOSAME)) {
          // if the key is not primary key, then
          // set rec_num to 2 if rec_num <= 1
          // todo for unique index, the rec_num should be 1
          rec_num = rec_num <= 1 ? 2 : rec_num;
        }
        key_info->rec_per_key[0] = rec_num;
      }
    }
  }
}

uint ha_federatedx::init_index_cardinality(federatedx_io *io) {
  DBUG_ENTER("ha_federatedx::init_index_cardinality");
  char show_index_query[FEDERATEDX_QUERY_BUFFER_SIZE];
  String query(show_index_query, sizeof(show_index_query), &my_charset_bin);
  query.length(0);
  query.append(STRING_WITH_LEN("SHOW index in "));
  query.append(STRING_WITH_LEN("`"));
  query.append(share->table_name, share->table_name_length);
  query.append(STRING_WITH_LEN("`"));
  FEDERATEDX_IO_ROW *row;
  FEDERATEDX_IO_RESULT *index_info = 0;
  int error;
  if (io->query(query.ptr(), query.length(), SCAN_MODE_DEFAULT, NULL)) {
    // if query fails, we set all the cardinality to 1, and try to get
    // the cardinality next time
    goto no_index_info;
  } else {
    index_info = io->store_result();
    if (!index_info || (io->get_num_fields(index_info) < 7)) {
      goto no_index_info;
    } else {
      my_ulonglong rownum = io->get_num_rows(index_info);
      if (rownum == 0) {
        goto no_index_info;
      } else {
        for (my_ulonglong i = 0; i < rownum; i++) {
          if (!(row = io->fetch_row(index_info, NULL))) {
            goto no_index_info;
          }
          //
          if (!io->is_column_null(row,2)) {
            const char* key_name = io->get_column_data(row, 2);
            int key_num;
            if ((key_num = find_index_num(key_name)) == -1) {
              continue;
            }
            if (!io->is_column_null(row, 6)) {
              ha_rows cardinality = (ha_rows) my_strtoll10(io->get_column_data(row, 6), (char**) 0, &error);
              index_cardinality[key_num] = cardinality;
            } else {
              // if no cardinality, set the default value to 1, which means we will never use this index
              index_cardinality[key_num] = 1;
            }
          }
        }
        index_cardinality_init = true;
      }
    }
  }
  io->free_result(index_info);
  DBUG_RETURN(0);

no_index_info:
  for (uint i = 0; i < table->s->keys; i++) {
    index_cardinality[i] = 1;
  }
  io->free_result(index_info);
  DBUG_RETURN(0);
}

void ha_federatedx::init_pk_info() {
  if (table->def_read_set.n_bits != table->s->fields) {
    return;
  }
  bool has_primary_key = MY_TEST(table->s->primary_key != MAX_KEY);
  if (has_primary_key) {
    if (pk_set.bitmap == NULL) {
      // first allocate bitmap
      uint field_count = table->s->fields;
      my_bitmap_map* bitmaps=
              (my_bitmap_map*) alloc_root(&table->mem_root, bitmap_buffer_size(field_count));
      my_bitmap_init(&pk_set, (my_bitmap_map*) bitmaps, field_count,
                     FALSE);
    }
    bitmap_clear_all(&pk_set);

    KEY pk = table->key_info[table->s->primary_key];
    for (uint j = 0; j < pk.user_defined_key_parts; j++) {
      Field *field = pk.key_part[j].field;
      bitmap_set_bit(&pk_set, field->field_index);
    }
    pk_num = pk.user_defined_key_parts;
  } else {
    pk_num = 0;
  }
}

bool ha_federatedx::need_init_table_status(THD *thd) {
  // case 1. table status is not init yet or stats.records <= 1
  if(!table_status_init || stats.records <= 1) {
    return true;
  }
  // case 2. the row changes
  ha_rows small_table_threshold = thd->variables.fedx_small_table_threshold;
  ha_rows reinit_table_status_threshold = thd->variables.fedx_reinit_table_status_threshold;
  if (records_at_init_time > small_table_threshold) {
    ha_rows delta = insert_records_since_init + update_records_since_init
                    + delete_records_since_init;

    if(delta * 100 > records_at_init_time * reinit_table_status_threshold) {
      return true;
    }
  }
  // case 3. re-init table status every 2 day
  if(thd->start_time - table_status_init_time >= 3600*24) {
    return true;
  }
  return false;
}

uint ha_federatedx::init_index_info(federatedx_io *io, THD *thd) {
  uint error_code = 0;
  if (!index_cardinality_init) {
    if ((error_code = init_index_cardinality(io))) {
      return error_code;
    }
    if (index_cardinality_init && optimizer_flag(thd, OPTIMIZER_SWITCH_FEDX_INIT_REC_PER_KEY)
        && optimizer_flag(thd, OPTIMIZER_SWITCH_FEDX_CBO_WITH_ACTUAL_RECORDS)) {
      //set up cardinality info in st_key
      init_rec_per_key();
    }
  }
  return error_code;
}

int ha_federatedx::info(uint flag)
{
  uint error_code;
  THD *thd= ha_thd();
  federatedx_txn *tmp_txn;
  federatedx_io *tmp_io= 0, **iop= 0;
  bool support_partial_read = optimizer_flag(thd, OPTIMIZER_SWITCH_FEDX_SHARDED_READ) ||
                         optimizer_flag(thd, OPTIMIZER_SWITCH_FEDX_RANGE_READ);
  bool cache_range_info = optimizer_flag(thd, OPTIMIZER_SWITCH_FEDX_CACHE_RANGE_INFO);
  DBUG_ENTER("ha_federatedx::info");

  error_code= ER_QUERY_ON_FOREIGN_DATA_SOURCE;
  
  // external_lock may not have been called so txn may not be set
  tmp_txn= get_txn(thd);

  /* we want not to show table status if not needed to do so */
  if (flag & (HA_STATUS_VARIABLE | HA_STATUS_CONST | HA_STATUS_AUTO))
  {
    if (!*(iop= &io) && (error_code= tmp_txn->acquire(share, thd, TRUE, (iop= &tmp_io))))
      goto fail;
  }

  if ((flag & HA_STATUS_VARIABLE) && (flag & HA_STATUS_INIT_FEDX_INFO) && (!strcasecmp((*iop)->get_scheme(), "vitess"))) {
    if (share->s->shard_num == 0) {
      if ((error_code = init_shard_info(*iop))) {
        goto fail;
      }
    }
  }

  if (flag & (HA_STATUS_VARIABLE | HA_STATUS_CONST))
  {
    /*
      size of IO operations (This is based on a good guess, no high science
      involved)
    */
    if (flag & HA_STATUS_CONST)
      stats.block_size= 4096;

    max_query_size = (*iop)->max_query_size();

    if(need_init_table_status(thd)) {

      //1. reset flags
      table_status_init = true;
      table_status_init_time = thd->start_time;
      records_at_init_time = 2;
      insert_records_since_init = 0;
      delete_records_since_init = 0;
      update_records_since_init = 0;
      // once table status is updated, the index cardinality should be refreshed
      index_cardinality_init = false;

      // 2. init table status info
      if ((*iop)->table_metadata(&stats, share->table_name,
                                 (uint)share->table_name_length, flag))
        goto error;
      records_per_shard = stats.records;

      if (!strcasecmp((*iop)->get_scheme(), "vitess") && share->s->shard_num > 0) {
        ha_rows records_mode = thd->variables.fedx_vitess_table_records_mode;
        ha_rows records_factor = thd->variables.fedx_vitess_table_records_factor;
        if (records_mode == 0) {
          //do nothing
        } else if (records_mode == 1) {
          stats.records = stats.records * share->s->shard_num;
        } else if (records_mode == 2) {
          stats.records = stats.records * records_factor;
        }
      }

      //3. update flags
      table_status_init = true;
      table_status_init_time = thd->start_time;
      records_at_init_time = stats.records;

      //4. init index info
      init_index_info(*iop, thd);
    }

    if (flag & HA_STATUS_INIT_FEDX_INFO) {
      init_index_info(*iop, thd);
    }

    if (pk_num < 0) {
      init_pk_info();
    }

    if (field_type == FIELD_TYPE_UNKNOWN) {
      Field **field;
      for (field= table->field; *field; field++) {
        enum_field_types type = (*field)->type();
        if (type == MYSQL_TYPE_TINY_BLOB ||
                type == MYSQL_TYPE_MEDIUM_BLOB ||
                type == MYSQL_TYPE_LONG_BLOB ||
                type == MYSQL_TYPE_BLOB) {
          if ((*field)->field_length > 65535 &&
                  ((*field)->charset() == NULL || (*field)->charset()->number == 63)) {
            field_type = FIELD_HAS_BLOB;
            break;
          }
        }
      }

      if (field_type == FIELD_TYPE_UNKNOWN) {
        field_type = FIELD_HAS_NO_BLOB;
      }
    }
  }

  if (flag & HA_STATUS_AUTO)
    stats.auto_increment_value= (*iop)->last_insert_id();

  if ((flag & HA_STATUS_VARIABLE) && (flag & HA_STATUS_INIT_FEDX_INFO) && (!strcasecmp((*iop)->get_scheme(), "vitess"))) {
    // vitess related information

    // 1. range info(global)
    if (cache_range_info && share->part_col_name == NULL) {
      if ((error_code = init_global_range_info(*iop))) {
        goto error;
      }
    }

    // 2. range info(local)
    if (support_partial_read && !cache_range_info && local_part_col_name == NULL) {
      if ((error_code = init_local_range_info(*iop))) {
        goto error;
      }
    }

    // 3. vindex info
    if (!vindex_init) {
      if (table->def_read_set.n_bits == table->s->fields) {
        if (vindex_set.bitmap == NULL) {
          uint field_count = table->s->fields;
          my_bitmap_map* bitmaps =
                  (my_bitmap_map *) alloc_root(&table->mem_root, bitmap_buffer_size(field_count));
          my_bitmap_init(&vindex_set, (my_bitmap_map *) bitmaps, field_count,
                         FALSE);
        }
        if ((error_code = init_vindex_info(*iop))) {
          goto error;
        }
      }
    }
  }
  /*
    If ::info created it's own transaction, close it. This happens in case
    of show table status;
  */
  tmp_txn->release(&tmp_io);

  DBUG_RETURN(0);

error:
  if (iop && *iop)
  {
    my_printf_error((*iop)->error_code(), "Received error: %d : %s", MYF(0),
                    (*iop)->error_code(), (*iop)->error_str());
  }
  else if (remote_error_number != -1 /* error already reported */)
  {
    error_code= remote_error_number;
    my_error(error_code, MYF(0), ER_THD(thd, error_code));
  }
fail:
  tmp_txn->release(&tmp_io);
  DBUG_RETURN(error_code);
}


/**
  @brief Handles extra signals from MySQL server

  @param[in] operation  Hint for storage engine

  @return Operation Status
  @retval 0     OK
 */
int ha_federatedx::extra(ha_extra_function operation)
{
  DBUG_ENTER("ha_federatedx::extra");
  switch (operation) {
  case HA_EXTRA_IGNORE_DUP_KEY:
    ignore_duplicates= TRUE;
    break;
  case HA_EXTRA_NO_IGNORE_DUP_KEY:
    insert_dup_update= FALSE;
    ignore_duplicates= FALSE;
    break;
  case HA_EXTRA_WRITE_CAN_REPLACE:
    replace_duplicates= TRUE;
    break;
  case HA_EXTRA_WRITE_CANNOT_REPLACE:
    /*
      We use this flag to ensure that we do not create an "INSERT IGNORE"
      statement when inserting new rows into the remote table.
    */
    replace_duplicates= FALSE;
    break;
  case HA_EXTRA_INSERT_WITH_UPDATE:
    insert_dup_update= TRUE;
    break;
  case HA_EXTRA_PREPARE_FOR_DROP:
    table_will_be_deleted = TRUE;
    break;
  default:
    /* do nothing */
    DBUG_PRINT("info",("unhandled operation: %d", (uint) operation));
  }
  DBUG_RETURN(0);
}


/**
  @brief Reset state of file to after 'open'.

  @detail This function is called after every statement for all tables
    used by that statement.

  @return Operation status
    @retval     0       OK
*/

int ha_federatedx::reset(void)
{
  THD *thd= ha_thd();
  int error = 0;

  insert_dup_update= FALSE;
  ignore_duplicates= FALSE;
  replace_duplicates= FALSE;
  position_called= FALSE;
  dynstr_trunc(&additionalFilter, additionalFilter.length);
  use_default_mrr = TRUE;
  partial_ppd = TRUE;
  if (thd) {
    scan_mode = optimizer_flag(thd, OPTIMIZER_SWITCH_FEDX_SCAN_MODE_OLAP) ? SCAN_MODE_OLAP : SCAN_MODE_OLTP;
  }
  partial_read_scan_mode = scan_mode;
  is_delete_update_target = FALSE;
  pr_info.sharded_offset = 0;
  pr_info.range_offset = 0;
  pr_info.partial_read_mode = PARTIAL_READ_NONE;
  partial_read_mode_by_hint = PARTIAL_READ_DEFAULT;
  has_equal_filter = false;
  local_part_col_name = NULL;
  local_part_value_num = 0;
  part_col = NULL;
  max_query_size = 0;

  if (stored_result)
    insert_dynamic(&results, (uchar*) &stored_result);
  stored_result= 0;

  if (results.elements || local_shard_info_result)
  {
    federatedx_txn *tmp_txn;
    federatedx_io *tmp_io= 0, **iop;

    // external_lock may not have been called so txn may not be set
    tmp_txn= get_txn(thd);

    if (!*(iop= &io) && (error= tmp_txn->acquire(share, thd, TRUE, (iop= &tmp_io))))
    {
      DBUG_ASSERT(0);                             // Fail when testing
      return error;
    }

    if (results.elements) {
      for (uint i = 0; i < results.elements; ++i) {
        FEDERATEDX_IO_RESULT *result = 0;
        get_dynamic(&results, (uchar *) &result, i);
        (*iop)->free_result(result);
      }
      reset_dynamic(&results);
    }
    if (local_shard_info_result) {
      (*iop)->free_result(local_shard_info_result);
      local_shard_info_result = NULL;
    }
    tmp_txn->release(&tmp_io);
  }

  return error;

}

/*
  Used to delete all rows in a table. Both for cases of truncate and
  for cases where the optimizer realizes that all rows will be
  removed as a result of a SQL statement.

  Called from item_sum.cc by Item_func_group_concat::clear(),
  Item_sum_count_distinct::clear(), and Item_func_group_concat::clear().
  Called from sql_delete.cc by mysql_delete().
  Called from sql_select.cc by JOIN::reinit().
  Called from sql_union.cc by st_select_lex_unit::exec().
*/

int ha_federatedx::delete_all_rows()
{
  THD *thd= ha_thd();
  char query_buffer[FEDERATEDX_QUERY_BUFFER_SIZE];
  String query(query_buffer, sizeof(query_buffer), &my_charset_bin);
  int error;
  DBUG_ENTER("ha_federatedx::delete_all_rows");

  query.length(0);

  query.set_charset(system_charset_info);
  if (thd->lex->sql_command == SQLCOM_TRUNCATE)
    query.append(STRING_WITH_LEN("TRUNCATE "));
  else
    query.append(STRING_WITH_LEN("DELETE FROM "));
  append_ident(&query, share->table_name, share->table_name_length,
               ident_quote_char);

  /* no need for savepoint in autocommit mode */
  if (!(thd->variables.option_bits & (OPTION_NOT_AUTOCOMMIT | OPTION_BEGIN)))
    txn->stmt_autocommit();

  /*
    TRUNCATE won't return anything in mysql_affected_rows
  */

  if ((error= txn->acquire(share, thd, FALSE, &io)))
    DBUG_RETURN(error);

  if (io->query(query.ptr(), query.length(), SCAN_MODE_OLTP, NULL))
  {
    DBUG_RETURN(stash_remote_error());
  }
  stats.deleted+= stats.records;
  stats.records= 0;
  DBUG_RETURN(0);
}


/*
  The idea with handler::store_lock() is the following:

  The statement decided which locks we should need for the table
  for updates/deletes/inserts we get WRITE locks, for SELECT... we get
  read locks.

  Before adding the lock into the table lock handler (see thr_lock.c)
  mysqld calls store lock with the requested locks.  Store lock can now
  modify a write lock to a read lock (or some other lock), ignore the
  lock (if we don't want to use MySQL table locks at all) or add locks
  for many tables (like we do when we are using a MERGE handler).

  Berkeley DB for federatedx  changes all WRITE locks to TL_WRITE_ALLOW_WRITE
  (which signals that we are doing WRITES, but we are still allowing other
  reader's and writer's.

  When releasing locks, store_lock() are also called. In this case one
  usually doesn't have to do anything.

  In some exceptional cases MySQL may send a request for a TL_IGNORE;
  This means that we are requesting the same lock as last time and this
  should also be ignored. (This may happen when someone does a flush
  table when we have opened a part of the tables, in which case mysqld
  closes and reopens the tables and tries to get the same locks at last
  time).  In the future we will probably try to remove this.

  Called from lock.cc by get_lock_data().
*/

THR_LOCK_DATA **ha_federatedx::store_lock(THD *thd,
                                         THR_LOCK_DATA **to,
                                         enum thr_lock_type lock_type)
{
  DBUG_ENTER("ha_federatedx::store_lock");
  if (lock_type != TL_IGNORE && lock.type == TL_UNLOCK)
  {
      statement_lock_type = lock_type;
    /*
      Here is where we get into the guts of a row level lock.
      If TL_UNLOCK is set
      If we are not doing a LOCK TABLE or DISCARD/IMPORT
      TABLESPACE, then allow multiple writers
    */

    if ((lock_type >= TL_WRITE_CONCURRENT_INSERT &&
         lock_type <= TL_WRITE) && !thd->in_lock_tables)
      lock_type= TL_WRITE_ALLOW_WRITE;

    /*
      In queries of type INSERT INTO t1 SELECT ... FROM t2 ...
      MySQL would use the lock TL_READ_NO_INSERT on t2, and that
      would conflict with TL_WRITE_ALLOW_WRITE, blocking all inserts
      to t2. Convert the lock to a normal read lock to allow
      concurrent inserts to t2.
    */

    if (lock_type == TL_READ_NO_INSERT && !thd->in_lock_tables)
      lock_type= TL_READ;

    lock.type= lock_type;
  }

  *to++= &lock;

  DBUG_RETURN(to);
}


static int test_connection(MYSQL_THD thd, federatedx_io *io,
                           FEDERATEDX_SHARE *share)
{
  char buffer[FEDERATEDX_QUERY_BUFFER_SIZE];
  String str(buffer, sizeof(buffer), &my_charset_bin);
  FEDERATEDX_IO_RESULT *resultset= NULL;
  int retval;

  str.length(0);
  str.append(STRING_WITH_LEN("SELECT /*!99999 select for mfed ddl*/ * FROM "));
  append_identifier(thd, &str, share->table_name,
                    share->table_name_length);
  str.append(STRING_WITH_LEN(" WHERE 1=0"));

  if ((retval= io->query(str.ptr(), str.length(), SCAN_MODE_DEFAULT, NULL)))
  {
    sprintf(buffer, "database: '%s'  username: '%s'  hostname: '%s'",
            share->database, share->username, share->hostname);
    DBUG_PRINT("info", ("error-code: %d", io->error_code()));
    my_error(ER_CANT_CREATE_FEDERATED_TABLE, MYF(0), buffer);
  }
  else
    resultset= io->store_result();

  io->free_result(resultset);

  return retval;
}

/*
  create() does nothing, since we have no local setup of our own.
  FUTURE: We should potentially connect to the foreign database and
*/

int ha_federatedx::create(const char *name, TABLE *table_arg,
                         HA_CREATE_INFO *create_info)
{
  int retval;
  THD *thd= ha_thd();
  FEDERATEDX_SHARE tmp_share; // Only a temporary share, to test the url
  federatedx_txn *tmp_txn;
  federatedx_io *tmp_io= NULL;
  DBUG_ENTER("ha_federatedx::create");

  if ((retval= parse_url(thd->mem_root, &tmp_share, table_arg->s, 1)))
    goto error;

  /* loopback socket connections hang due to LOCK_open mutex */
  if ((!tmp_share.hostname || !strcmp(tmp_share.hostname,my_localhost)) &&
      !tmp_share.port)
    goto error;

  /*
    If possible, we try to use an existing network connection to
    the remote server. To ensure that no new FEDERATEDX_SERVER
    instance is created, we pass NULL in get_server() TABLE arg.
  */
  mysql_mutex_lock(&federatedx_mutex);
  tmp_share.s= get_server(&tmp_share, NULL);
  mysql_mutex_unlock(&federatedx_mutex);

  if (tmp_share.s)
  {
    tmp_txn= get_txn(thd);
    if (!(retval= tmp_txn->acquire(&tmp_share, thd, TRUE, &tmp_io)))
    {
      retval= test_connection(thd, tmp_io, &tmp_share);
      tmp_txn->release(&tmp_io);
    }
    free_server(tmp_txn, tmp_share.s);
  }
  else
  {
    FEDERATEDX_SERVER server;

    fill_server(thd->mem_root, &server, &tmp_share, create_info->table_charset);

#ifndef DBUG_OFF
    mysql_mutex_init(fe_key_mutex_FEDERATEDX_SERVER_mutex,
                     &server.mutex, MY_MUTEX_INIT_FAST);
    mysql_mutex_lock(&server.mutex);
#endif

    tmp_io= federatedx_io::construct(thd->mem_root, &server);

    retval= test_connection(thd, tmp_io, &tmp_share);

#ifndef DBUG_OFF
    mysql_mutex_unlock(&server.mutex);
    mysql_mutex_destroy(&server.mutex);
#endif

    delete tmp_io;
  }

error:
  DBUG_RETURN(retval);

}


int ha_federatedx::stash_remote_error()
{
  DBUG_ENTER("ha_federatedx::stash_remote_error()");
  if (!io)
    DBUG_RETURN(remote_error_number);
  remote_error_number= io->error_code();
  strmake_buf(remote_error_buf, io->error_str());
  if (remote_error_number == ER_DUP_ENTRY ||
      remote_error_number == ER_DUP_KEY)
    DBUG_RETURN(HA_ERR_FOUND_DUPP_KEY);
  DBUG_RETURN(HA_FEDERATEDX_ERROR_WITH_REMOTE_SYSTEM);
}


bool ha_federatedx::get_error_message(int error, String* buf)
{
  DBUG_ENTER("ha_federatedx::get_error_message");
  DBUG_PRINT("enter", ("error: %d", error));
  if (error == HA_FEDERATEDX_ERROR_WITH_REMOTE_SYSTEM)
  {
    buf->append(STRING_WITH_LEN("Error on remote system: "));
    buf->qs_append(remote_error_number);
    buf->append(STRING_WITH_LEN(": "));
    buf->append(remote_error_buf);
    /* Ensure string ends with \0 */
    (void) buf->c_ptr_safe();

    remote_error_number= 0;
    remote_error_buf[0]= '\0';
  }
  DBUG_PRINT("exit", ("message: %s", buf->c_ptr_safe()));
  DBUG_RETURN(FALSE);
}


int ha_federatedx::start_stmt(MYSQL_THD thd, thr_lock_type lock_type)
{
  DBUG_ENTER("ha_federatedx::start_stmt");
  DBUG_ASSERT(txn == get_txn(thd));
  
  if (!txn->in_transaction())
  {
    txn->stmt_begin();
    trans_register_ha(thd, FALSE, ht);
  }
  DBUG_RETURN(0);
}


int ha_federatedx::external_lock(MYSQL_THD thd, int lock_type)
{
  int error= 0;
  DBUG_ENTER("ha_federatedx::external_lock");

  if (lock_type == F_UNLCK)
    txn->release(&io);
  else
  {
    table_will_be_deleted = FALSE;
    txn= get_txn(thd);  
    if (!(error= txn->acquire(share, ha_thd(), lock_type == F_RDLCK, &io)) &&
        (lock_type == F_WRLCK || !io->is_autocommit()))
    {
      if (!thd_test_options(thd, (OPTION_NOT_AUTOCOMMIT | OPTION_BEGIN)))
      {
        txn->stmt_begin();
        trans_register_ha(thd, FALSE, ht);
      }
      else
      {
        txn->txn_begin();
        trans_register_ha(thd, TRUE, ht);
      }
    }
  }

  DBUG_RETURN(error);
}


int ha_federatedx::savepoint_set(handlerton *hton, MYSQL_THD thd, void *sv)
{
  int error= 0;
  federatedx_txn *txn= (federatedx_txn *) thd_get_ha_data(thd, hton);
  DBUG_ENTER("ha_federatedx::savepoint_set");

  if (txn && txn->has_connections())
  {
    if (txn->txn_begin())
      trans_register_ha(thd, TRUE, hton);
    
    txn->sp_acquire((ulong *) sv);

    DBUG_ASSERT(1 < *(ulong *) sv);
  }

  DBUG_RETURN(error);
}


int ha_federatedx::savepoint_rollback(handlerton *hton, MYSQL_THD thd, void *sv)
 {
  int error= 0;
  federatedx_txn *txn= (federatedx_txn *) thd_get_ha_data(thd, hton);
  DBUG_ENTER("ha_federatedx::savepoint_rollback");
  
  if (txn)
    error= txn->sp_rollback((ulong *) sv);

  DBUG_RETURN(error);
}


int ha_federatedx::savepoint_release(handlerton *hton, MYSQL_THD thd, void *sv)
{
  int error= 0;
  federatedx_txn *txn= (federatedx_txn *) thd_get_ha_data(thd, hton);
  DBUG_ENTER("ha_federatedx::savepoint_release");
  
  if (txn)
    error= txn->sp_release((ulong *) sv);

  DBUG_RETURN(error);
}


int ha_federatedx::commit(handlerton *hton, MYSQL_THD thd, bool all)
{
  int return_val;
  federatedx_txn *txn= (federatedx_txn *) thd_get_ha_data(thd, hton);
  DBUG_ENTER("ha_federatedx::commit");

  if (all)
    return_val= txn->txn_commit();
  else
    return_val= txn->stmt_commit();    
  
  DBUG_PRINT("info", ("error val: %d", return_val));
  DBUG_RETURN(return_val);
}


int ha_federatedx::rollback(handlerton *hton, MYSQL_THD thd, bool all)
{
  int return_val;
  federatedx_txn *txn= (federatedx_txn *) thd_get_ha_data(thd, hton);
  DBUG_ENTER("ha_federatedx::rollback");

  if (all)
    return_val= txn->txn_rollback();
  else
    return_val= txn->stmt_rollback();

  DBUG_PRINT("info", ("error val: %d", return_val));
  DBUG_RETURN(return_val);
}


/*
  Federated supports assisted discovery, like
  CREATE TABLE t1 CONNECTION="mysql://joe:pass@192.168.1.111/federated/t1";
  but not a fully automatic discovery where a table magically appear
  on any use (like, on SELECT * from t1).
*/
int ha_federatedx::discover_assisted(handlerton *hton, THD* thd,
                                TABLE_SHARE *table_s, HA_CREATE_INFO *info)
{
  int error= HA_ERR_NO_CONNECTION;
  FEDERATEDX_SHARE tmp_share;
  CHARSET_INFO *cs= system_charset_info;
  MYSQL mysql;
  char buf[1024];
  String query(buf, sizeof(buf), cs);
  static LEX_CSTRING cut_clause={STRING_WITH_LEN(" WITH SYSTEM VERSIONING")};
  int cut_offset;
  MYSQL_RES *res;
  MYSQL_ROW rdata;
  ulong *rlen;
  my_bool my_true= 1;

  if (parse_url(thd->mem_root, &tmp_share, table_s, 1))
    return HA_WRONG_CREATE_OPTION;

  mysql_init(&mysql);
  mysql_options(&mysql, MYSQL_SET_CHARSET_NAME, cs->csname);
  mysql_options(&mysql, MYSQL_OPT_USE_THREAD_SPECIFIC_MEMORY, (char*)&my_true);

  if (!mysql_real_connect(&mysql, tmp_share.hostname, tmp_share.username,
                          tmp_share.password, tmp_share.database,
                          tmp_share.port, tmp_share.socket, 0))
    goto err1;
  
  if (mysql_real_query(&mysql, STRING_WITH_LEN("SET SQL_MODE=NO_TABLE_OPTIONS")))
    goto err1;

  query.copy(STRING_WITH_LEN("SHOW CREATE TABLE "), cs);
  append_ident(&query, tmp_share.table_name, 
               tmp_share.table_name_length, ident_quote_char);

  if (mysql_real_query(&mysql, query.ptr(), query.length()))
    goto err1;

  if (!((res= mysql_store_result(&mysql))))
    goto err1;

  if (!(rdata= mysql_fetch_row(res)) || !((rlen= mysql_fetch_lengths(res))))
    goto err2;

  query.copy(rdata[1], rlen[1], cs);
  cut_offset= (int)query.length() - (int)cut_clause.length;
  if (cut_offset > 0 && !memcmp(query.ptr() + cut_offset,
                                cut_clause.str, cut_clause.length))
    query.length(cut_offset);
  //add engine explicitly in case 'set sql_mode=no_table_options' does not work
  query.append(STRING_WITH_LEN(" ENGINE=FEDERATED"), cs);
  query.append(STRING_WITH_LEN(" CONNECTION='"), cs);
  query.append_for_single_quote(table_s->connect_string.str,
                                table_s->connect_string.length);
  query.append('\'');

  error= table_s->init_from_sql_statement_string(thd, true,
                                                 query.ptr(), query.length());

err2:
  mysql_free_result(res);
err1:
  if (error)
    my_error(ER_CONNECT_TO_FOREIGN_DATA_SOURCE, MYF(0), mysql_error(&mysql));
  mysql_close(&mysql);
  return error;
}


struct st_mysql_storage_engine federatedx_storage_engine=
{ MYSQL_HANDLERTON_INTERFACE_VERSION };

maria_declare_plugin(federatedx)
{
  MYSQL_STORAGE_ENGINE_PLUGIN,
  &federatedx_storage_engine,
  "FEDERATED",
  "Patrick Galbraith",
  "FederatedX pluggable storage engine",
  PLUGIN_LICENSE_GPL,
  federatedx_db_init, /* Plugin Init */
  federatedx_done, /* Plugin Deinit */
  0x0201 /* 2.1 */,
  NULL,                       /* status variables                */
  NULL,                       /* system variables                */
  "2.1",                      /* string version */
  MariaDB_PLUGIN_MATURITY_STABLE /* maturity */
}
maria_declare_plugin_end;
