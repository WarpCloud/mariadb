/*
Copyright (c) 2007, Antony T Curtis
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

    * Neither the name of FederatedX nor the names of its
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


#define MYSQL_SERVER 1
#include <my_global.h>
#include "sql_priv.h"
#include <mysqld_error.h>
#include <field.h>

#include "ha_federatedx.h"

#include "m_string.h"
#include "mysqld_error.h"
#include "sql_servers.h"
#include <sql_class.h>

#ifdef USE_PRAGMA_IMPLEMENTATION
#pragma implementation                          // gcc: Class implementation
#endif


#define SAVEPOINT_REALIZED  1
#define SAVEPOINT_RESTRICT  2
#define SAVEPOINT_EMITTED 4


typedef struct federatedx_savepoint
{
  ulong level;
  uint  flags;
} SAVEPT;

struct mysql_position
{
  MYSQL_RES* result;
  MYSQL_ROW_OFFSET offset;
};


class federatedx_io_mysql :public federatedx_io
{
protected:
  MYSQL mysql; /* MySQL connection */
  DYNAMIC_ARRAY savepoints;
  bool requested_autocommit;
  bool actual_autocommit;

  virtual int actual_query(const char *buffer, size_t length, void *info);
  bool test_all_restrict() const;
public:
  federatedx_io_mysql(FEDERATEDX_SERVER *);
  ~federatedx_io_mysql();

  int simple_query(const char *fmt, ...);
  int query(const char *buffer, size_t length, int scan_mode, void *scan_info);
  virtual FEDERATEDX_IO_RESULT *store_result();

  virtual size_t max_query_size() const;

  virtual my_ulonglong affected_rows() const;
  virtual my_ulonglong last_insert_id() const;

  virtual int error_code();
  virtual const char *error_str();

  void reset();
  int commit();
  int rollback();

  int savepoint_set(ulong sp);
  ulong savepoint_release(ulong sp);
  virtual void check_persistence_connect();
  ulong savepoint_rollback(ulong sp);
  void savepoint_restrict(ulong sp);

  ulong last_savepoint() const;
  ulong actual_savepoint() const;
  bool is_autocommit() const;

  bool table_metadata(ha_statistics *stats, const char *table_name,
                      uint table_name_length, uint flag);

  /* resultset operations */

  virtual void free_result(FEDERATEDX_IO_RESULT *io_result);
  virtual unsigned int get_num_fields(FEDERATEDX_IO_RESULT *io_result);
  virtual my_ulonglong get_num_rows(FEDERATEDX_IO_RESULT *io_result);
  virtual FEDERATEDX_IO_ROW *fetch_row(FEDERATEDX_IO_RESULT *io_result, void **current);
  virtual ulong *fetch_lengths(FEDERATEDX_IO_RESULT *io_result);
  virtual const char *get_column_data(FEDERATEDX_IO_ROW *row,
                                      unsigned int column);
  virtual bool is_column_null(const FEDERATEDX_IO_ROW *row,
                              unsigned int column) const;

  virtual size_t get_ref_length() const;
  virtual void mark_position(FEDERATEDX_IO_RESULT *io_result,
                             void *ref, void *offset);
  virtual int seek_position(FEDERATEDX_IO_RESULT **io_result,
                            const void *ref);
  virtual void set_thd(void *thd);
  virtual int mysql_connect();
};


federatedx_io *instantiate_io_mysql(MEM_ROOT *server_root,
                                    FEDERATEDX_SERVER *server)
{
  return new (server_root) federatedx_io_mysql(server);
}

federatedx_io_mysql::federatedx_io_mysql(FEDERATEDX_SERVER *aserver)
  : federatedx_io(aserver),
    requested_autocommit(TRUE), actual_autocommit(TRUE)
{
  DBUG_ENTER("federatedx_io_mysql::federatedx_io_mysql");

  bzero(&mysql, sizeof(MYSQL));
  bzero(&savepoints, sizeof(DYNAMIC_ARRAY));

  my_init_dynamic_array(&savepoints, sizeof(SAVEPT), 16, 16, MYF(0));

  DBUG_VOID_RETURN;
}


federatedx_io_mysql::~federatedx_io_mysql()
{
  DBUG_ENTER("federatedx_io_mysql::~federatedx_io_mysql");

  mysql_close(&mysql);
  delete_dynamic(&savepoints);

  DBUG_VOID_RETURN;
}


void federatedx_io_mysql::reset()
{
  reset_dynamic(&savepoints);
  set_active(FALSE);
  
  requested_autocommit= TRUE;
  mysql.reconnect= 1;
}


int federatedx_io_mysql::commit()
{
  int error= 0;
  DBUG_ENTER("federatedx_io_mysql::commit");

  if (!actual_autocommit && (error= actual_query("COMMIT", 6, NULL)))
    rollback();
  
  reset();
  
  DBUG_RETURN(error);
}

int federatedx_io_mysql::rollback()
{
  int error= 0;
  DBUG_ENTER("federatedx_io_mysql::rollback");
  
  if (!actual_autocommit)
    error= actual_query("ROLLBACK", 8, NULL);
  else
    error= ER_WARNING_NOT_COMPLETE_ROLLBACK;

  reset();
  
  DBUG_RETURN(error);
}


ulong federatedx_io_mysql::last_savepoint() const
{
  SAVEPT *savept= NULL;
  DBUG_ENTER("federatedx_io_mysql::last_savepoint");

  if (savepoints.elements)
    savept= dynamic_element(&savepoints, savepoints.elements - 1, SAVEPT *);

  DBUG_RETURN(savept ? savept->level : 0);
}

void federatedx_io_mysql::check_persistence_connect() {
  if (!mysql.net.vio) {
    actual_autocommit = true;
  }
}

ulong federatedx_io_mysql::actual_savepoint() const
{
  SAVEPT *savept= NULL;
  uint index= savepoints.elements;
  DBUG_ENTER("federatedx_io_mysql::last_savepoint");

  while (index)
  {
    savept= dynamic_element(&savepoints, --index, SAVEPT *);
    if (savept->flags & SAVEPOINT_REALIZED)
    break;
  savept= NULL;
  }

  DBUG_RETURN(savept ? savept->level : 0);
}

bool federatedx_io_mysql::is_autocommit() const
{
  return actual_autocommit;
}


int federatedx_io_mysql::savepoint_set(ulong sp)
{
  int error;
  SAVEPT savept;
  DBUG_ENTER("federatedx_io_mysql::savepoint_set");
  DBUG_PRINT("info",("savepoint=%lu", sp));
  DBUG_ASSERT(sp > last_savepoint());

  savept.level= sp;
  savept.flags= 0;

  if ((error= insert_dynamic(&savepoints, (uchar*) &savept) ? -1 : 0))
    goto err;

  set_active(TRUE);
  mysql.reconnect= 0;
  requested_autocommit= FALSE;

err:
  DBUG_RETURN(error);
}


ulong federatedx_io_mysql::savepoint_release(ulong sp)
{
  SAVEPT *savept, *last= NULL;
  DBUG_ENTER("federatedx_io_mysql::savepoint_release");
  DBUG_PRINT("info",("savepoint=%lu", sp));
  
  while (savepoints.elements)
  {
    savept= dynamic_element(&savepoints, savepoints.elements - 1, SAVEPT *);
    if (savept->level < sp)
      break;
    if ((savept->flags & (SAVEPOINT_REALIZED | SAVEPOINT_RESTRICT)) == SAVEPOINT_REALIZED)
      last= savept;
    savepoints.elements--;
  }

  if (last)
  {
    char buffer[STRING_BUFFER_USUAL_SIZE];
    size_t length= my_snprintf(buffer, sizeof(buffer),
              "RELEASE SAVEPOINT save%lu", last->level);
    actual_query(buffer, length, NULL);
  }

  DBUG_RETURN(last_savepoint()); 
}


ulong federatedx_io_mysql::savepoint_rollback(ulong sp)
{
  SAVEPT *savept;
  uint index;
  DBUG_ENTER("federatedx_io_mysql::savepoint_release");
  DBUG_PRINT("info",("savepoint=%lu", sp));
  
  while (savepoints.elements)
  {
    savept= dynamic_element(&savepoints, savepoints.elements - 1, SAVEPT *);
    if (savept->level <= sp)
      break;
    savepoints.elements--;
  }

  for (index= savepoints.elements, savept= NULL; index;)
  {
    savept= dynamic_element(&savepoints, --index, SAVEPT *);
    if (savept->flags & SAVEPOINT_REALIZED)
    break;
  savept= NULL;
  }
  
  if (savept && !(savept->flags & SAVEPOINT_RESTRICT))
  {
    char buffer[STRING_BUFFER_USUAL_SIZE];
    size_t length= my_snprintf(buffer, sizeof(buffer),
              "ROLLBACK TO SAVEPOINT save%lu", savept->level);
    actual_query(buffer, length, NULL);
  }

  DBUG_RETURN(last_savepoint());
}


void federatedx_io_mysql::savepoint_restrict(ulong sp)
{
  SAVEPT *savept;
  uint index= savepoints.elements;
  DBUG_ENTER("federatedx_io_mysql::savepoint_restrict");
  
  while (index)
  {
    savept= dynamic_element(&savepoints, --index, SAVEPT *);
  if (savept->level > sp)
    continue;
  if (savept->level < sp)
    break;
  savept->flags|= SAVEPOINT_RESTRICT;
  break;
  }
  
  DBUG_VOID_RETURN;
}


int federatedx_io_mysql::simple_query(const char *fmt, ...)
{
  char buffer[STRING_BUFFER_USUAL_SIZE];
  size_t length;
  int error;
  va_list arg;
  DBUG_ENTER("federatedx_io_mysql::simple_query");

  va_start(arg, fmt);  
  length= my_vsnprintf(buffer, sizeof(buffer), fmt, arg);
  va_end(arg);
  
  error= query(buffer, length, SCAN_MODE_DEFAULT, NULL);
  
  DBUG_RETURN(error);
}


bool federatedx_io_mysql::test_all_restrict() const
{
  bool result= FALSE;
  SAVEPT *savept;
  uint index= savepoints.elements;
  DBUG_ENTER("federatedx_io_mysql::test_all_restrict");
  
  while (index)
  {
    savept= dynamic_element(&savepoints, --index, SAVEPT *);
  if ((savept->flags & (SAVEPOINT_REALIZED | 
                        SAVEPOINT_RESTRICT)) == SAVEPOINT_REALIZED ||
    (savept->flags & SAVEPOINT_EMITTED))
      DBUG_RETURN(FALSE);
    if (savept->flags & SAVEPOINT_RESTRICT)
    result= TRUE;
  }
  
  DBUG_RETURN(result); 
}


int federatedx_io_mysql::query(const char *buffer, size_t length, int scan_mode, void *scan_info)
{
  int error;
  check_persistence_connect();
  bool wants_autocommit= requested_autocommit | is_readonly();
  DBUG_ENTER("federatedx_io_mysql::query");

  if (!wants_autocommit && test_all_restrict())
    wants_autocommit= TRUE;
  if (is_active()) {
    // if we are inside a transaction, we should never want autocommit, even if the query is read only
    wants_autocommit = false;
  }

  if (wants_autocommit != actual_autocommit)
  {
    if ((error= actual_query(wants_autocommit ? "SET AUTOCOMMIT=1"
                                            : "SET AUTOCOMMIT=0", 16, NULL)))
    DBUG_RETURN(error);                         
    mysql.reconnect= wants_autocommit ? 1 : 0;
    actual_autocommit= wants_autocommit;
  }
  
  if (!actual_autocommit && last_savepoint() != actual_savepoint())
  {
    SAVEPT *savept= dynamic_element(&savepoints, savepoints.elements - 1, 
                                SAVEPT *);
    if (!(savept->flags & SAVEPOINT_RESTRICT))
  {
      char buf[STRING_BUFFER_USUAL_SIZE];
      size_t len= my_snprintf(buf, sizeof(buf),
                  "SAVEPOINT save%lu", savept->level);
      if ((error= actual_query(buf, len, NULL)))
    DBUG_RETURN(error);                         
    set_active(TRUE);
    savept->flags|= SAVEPOINT_EMITTED;
    }
    savept->flags|= SAVEPOINT_REALIZED;
  }

  if (!(error= actual_query(buffer, length, NULL)))
    set_active(is_active() || !actual_autocommit);

  DBUG_RETURN(error);
}


int federatedx_io_mysql::actual_query(const char *buffer, size_t length, void *info)
{
  int error;
  DBUG_ENTER("federatedx_io_mysql::actual_query");

  if (!mysql.net.vio)
  {
    error = mysql_connect();
    if (error) {
      DBUG_RETURN(error);
    }
  }

  if (!(error= mysql_real_query(&mysql, STRING_WITH_LEN("set time_zone='+00:00'"))))
    error= mysql_real_query(&mysql, buffer, (ulong)length);
  
  DBUG_RETURN(error);
}

size_t federatedx_io_mysql::max_query_size() const
{
  return mysql.net.max_packet_size;
}


my_ulonglong federatedx_io_mysql::affected_rows() const
{
  return mysql.affected_rows;
}


my_ulonglong federatedx_io_mysql::last_insert_id() const
{
  return mysql.insert_id;
}


int federatedx_io_mysql::error_code()
{
  return mysql_errno(&mysql);
}


const char *federatedx_io_mysql::error_str()
{
  return mysql_error(&mysql);
}

FEDERATEDX_IO_RESULT *federatedx_io_mysql::store_result()
{
  FEDERATEDX_IO_RESULT *result;
  DBUG_ENTER("federatedx_io_mysql::store_result");

  result= (FEDERATEDX_IO_RESULT *) mysql_store_result(&mysql);

  DBUG_RETURN(result);
}


void federatedx_io_mysql::free_result(FEDERATEDX_IO_RESULT *io_result)
{
  mysql_free_result((MYSQL_RES *) io_result);
}


unsigned int federatedx_io_mysql::get_num_fields(FEDERATEDX_IO_RESULT *io_result)
{
  return mysql_num_fields((MYSQL_RES *) io_result);
}


my_ulonglong federatedx_io_mysql::get_num_rows(FEDERATEDX_IO_RESULT *io_result)
{
  return mysql_num_rows((MYSQL_RES *) io_result);
}


FEDERATEDX_IO_ROW *federatedx_io_mysql::fetch_row(FEDERATEDX_IO_RESULT *io_result, void **current)
{
  MYSQL_RES *result= (MYSQL_RES*)io_result;
  if (current) {
    *current = result->data_cursor;
  }
  return (FEDERATEDX_IO_ROW *) mysql_fetch_row(result);
}


ulong *federatedx_io_mysql::fetch_lengths(FEDERATEDX_IO_RESULT *io_result)
{
  return mysql_fetch_lengths((MYSQL_RES *) io_result);
}


const char *federatedx_io_mysql::get_column_data(FEDERATEDX_IO_ROW *row,
                                                 unsigned int column)
{
  return ((MYSQL_ROW)row)[column];
}


bool federatedx_io_mysql::is_column_null(const FEDERATEDX_IO_ROW *row,
                                         unsigned int column) const
{
  return !((MYSQL_ROW)row)[column];
}

bool federatedx_io_mysql::table_metadata(ha_statistics *stats,
                                         const char *table_name,
                                         uint table_name_length, uint flag)
{
  char status_buf[FEDERATEDX_QUERY_BUFFER_SIZE];
  FEDERATEDX_IO_RESULT *result= 0;
  FEDERATEDX_IO_ROW *row;
  String status_query_string(status_buf, sizeof(status_buf), &my_charset_bin);
  int error;

  status_query_string.length(0);
  status_query_string.append(STRING_WITH_LEN("SHOW TABLE STATUS LIKE "));
  append_ident(&status_query_string, table_name,
               table_name_length, value_quote_char);

  if (query(status_query_string.ptr(), status_query_string.length(), SCAN_MODE_EITHER, NULL))
    goto error;

  status_query_string.length(0);

  result= store_result();

  /*
    We're going to use fields num. 4, 12 and 13 of the resultset,
    so make sure we have these fields.
  */
  if (!result || (get_num_fields(result) < 14))
    goto error;

  if (!get_num_rows(result))
    goto error;

  if (!(row= fetch_row(result, NULL)))
    goto error;

  /*
    deleted is set in ha_federatedx::info
  */
  /*
    need to figure out what this means as far as federatedx is concerned,
    since we don't have a "file"

    data_file_length = ?
    index_file_length = ?
    delete_length = ?
  */
  if (!is_column_null(row, 4))
    stats->records= (ha_rows) my_strtoll10(get_column_data(row, 4),
	                                   (char**) 0, &error);
  if (stats->records == 0) {
    stats->records = FEDERATEDX_RECORDS_IN_RANGE;
  }

  if (!is_column_null(row, 5))
    stats->mean_rec_length= (ulong) my_strtoll10(get_column_data(row, 5),
	                                         (char**) 0, &error);

  stats->data_file_length= stats->records * stats->mean_rec_length;

  if (!is_column_null(row, 12))
    stats->update_time= (time_t) my_strtoll10(get_column_data(row, 12),
	                                      (char**) 0, &error);
  if (!is_column_null(row, 13))
    stats->check_time= (time_t) my_strtoll10(get_column_data(row, 13),
	                                     (char**) 0, &error);

  free_result(result);
  return 0;

error:
  if (!mysql_errno(&mysql))
  {
    mysql.net.last_errno= ER_NO_SUCH_TABLE;
    strmake_buf(mysql.net.last_error, "Remote table does not exist");
  }
  free_result(result);
  return 1;
}



size_t federatedx_io_mysql::get_ref_length() const
{
  return sizeof(mysql_position);
}


void federatedx_io_mysql::mark_position(FEDERATEDX_IO_RESULT *io_result,
                                        void *ref, void *offset)
{
  mysql_position& pos= *reinterpret_cast<mysql_position*>(ref);
  pos.result= (MYSQL_RES *) io_result;
  pos.offset= reinterpret_cast<MYSQL_ROW_OFFSET>(offset);
}

int federatedx_io_mysql::seek_position(FEDERATEDX_IO_RESULT **io_result,
                                       const void *ref)
{
  const mysql_position& pos= *reinterpret_cast<const mysql_position*>(ref);

  if (!pos.result || !pos.offset)
    return HA_ERR_END_OF_FILE;

  pos.result->current_row= 0;
  pos.result->data_cursor= pos.offset;
  *io_result= (FEDERATEDX_IO_RESULT*) pos.result;

  return 0;
}

void federatedx_io_mysql::set_thd(void *thd)
{
  mysql.net.thd= thd;
}

int federatedx_io_mysql::mysql_connect()
{
  DBUG_ENTER("federatedx_io_vitess::mysql_connect");

  //todo should throw error if current io is in transaction
  my_bool my_true= 1;

  if (!(mysql_init(&mysql)))
    DBUG_RETURN(-1);

  /*
  BUG# 17044 Federated Storage Engine is not UTF8 clean
  Add set names to whatever charset the table is at open
  of table
  */
  /* this sets the csname like 'set names utf8' */
  mysql_options(&mysql, MYSQL_SET_CHARSET_NAME, get_charsetname());
  mysql_options(&mysql, MYSQL_OPT_USE_THREAD_SPECIFIC_MEMORY,
                (char*) &my_true);
  mysql_options(&mysql, MYSQL_ENABLE_CLEARTEXT_PLUGIN, (char*) &my_true);

  if (!mysql_real_connect(&mysql,
                          get_hostname(),
                          get_username(),
                          get_password(),
                          get_database(),
                          get_port(),
                          get_socket(), 0))
    DBUG_RETURN(ER_CONNECT_TO_FOREIGN_DATA_SOURCE);
  mysql.reconnect= 1;
  actual_autocommit = true;

  DBUG_RETURN(0);
}

class federatedx_io_vitess :public federatedx_io_mysql {
    int current_scan_mode;
    bool in_shard_db;
    DYNAMIC_STRING session;
public:
    federatedx_io_vitess(FEDERATEDX_SERVER *);
    ~federatedx_io_vitess();

    int query(const char *buffer, size_t length, int scan_mode, void *scan_info);
    virtual void check_persistence_connect();
    int mysql_connect();
    int actual_query(const char *buffer, size_t length, void *info);
    int actual_query(const char *buffer, size_t length, void *info, bool init_session_status);
    int init_session();

};

federatedx_io *instantiate_io_vitess(MEM_ROOT *server_root, FEDERATEDX_SERVER *server) {
  return new (server_root) federatedx_io_vitess(server);
}

federatedx_io_vitess::federatedx_io_vitess(FEDERATEDX_SERVER *aserver) : federatedx_io_mysql(aserver)
{
  DBUG_ENTER("federatedx_io_vitess::federatedx_io_vitess");
  current_scan_mode = SCAN_MODE_UNKNOWN;
  in_shard_db = false;
  init_dynamic_string(&session, NULL, 1024, 80);
  dynstr_set(&session, 0);
  dynstr_append(&session, "{}");
  DBUG_VOID_RETURN;
}

federatedx_io_vitess::~federatedx_io_vitess() {
  DBUG_ENTER("federatedx_io_vitess::~federatedx_io_vitess");
  dynstr_free(&session);
  DBUG_VOID_RETURN;
}

void federatedx_io_vitess::check_persistence_connect() {
  if (!mysql.net.vio) {
    actual_autocommit = true;
    current_scan_mode = SCAN_MODE_OLTP;
  }
}

int federatedx_io_vitess::query(const char *buffer, size_t length, int scan_mode, void *scan_info) {
  int error;
  check_persistence_connect();
  bool wants_autocommit= requested_autocommit | is_readonly();
  DBUG_ENTER("federatedx_io_vitess::query");

  partial_read_info* pr_info = NULL;
  if (scan_info) {
    pr_info = (partial_read_info *) scan_info;
  }

  if (!wants_autocommit && test_all_restrict())
    wants_autocommit= TRUE;
  if (is_active()) {
    // if we are inside a transaction, we should never want autocommit, even if the query is read only
    wants_autocommit = false;
  }

  if (wants_autocommit != actual_autocommit)
  {
    if ((error= actual_query(wants_autocommit ? "SET AUTOCOMMIT=1"
                                            : "SET AUTOCOMMIT=0", 16, NULL)))
    DBUG_RETURN(error);
    mysql.reconnect= wants_autocommit ? 1 : 0;
    actual_autocommit= wants_autocommit;
  }

  if (!actual_autocommit && last_savepoint() != actual_savepoint())
  {
    SAVEPT *savept= dynamic_element(&savepoints, savepoints.elements - 1,
                                SAVEPT *);
    if (!(savept->flags & SAVEPOINT_RESTRICT))
  {
      char buf[STRING_BUFFER_USUAL_SIZE];
    int len= my_snprintf(buf, sizeof(buf),
                  "SAVEPOINT save%lu", savept->level);
      if ((error= actual_query(buf, len, NULL)))
    DBUG_RETURN(error);
    set_active(TRUE);
    savept->flags|= SAVEPOINT_EMITTED;
    }
    savept->flags|= SAVEPOINT_REALIZED;
  }

  if (scan_mode == SCAN_MODE_OLAP && current_scan_mode != SCAN_MODE_OLAP) {
    if ((error = actual_query("SET WORKLOAD='OLAP'", 19, NULL))) {
      DBUG_RETURN(error);
    }
    current_scan_mode = SCAN_MODE_OLAP;
  }
  if (scan_mode == SCAN_MODE_OLTP && current_scan_mode != SCAN_MODE_OLTP) {
    if ((error = actual_query("SET WORKLOAD='OLTP'", 19, NULL))) {
      DBUG_RETURN(error);
    }
    current_scan_mode = SCAN_MODE_OLTP;
  }

  if (!(error = actual_query(buffer, length, pr_info)))
    set_active(is_active() || !actual_autocommit);

  DBUG_RETURN(error);
}

int federatedx_io_vitess::mysql_connect()
{
  DBUG_ENTER("federatedx_io_vitess::mysql_connect");

  int error = federatedx_io_mysql::mysql_connect();
  dynstr_set(&session, 0);
  dynstr_append(&session, "{}");

  if (error) {
    DBUG_RETURN(error);
  }

  // once reconnect, reset the current workload flag
  current_scan_mode = SCAN_MODE_UNKNOWN;

  DBUG_RETURN(0);
}

const char* construct_partial_read_query(String *query, partial_read_info *pr_info) {
  const char* ret = NULL;
  if (pr_info->partial_read_mode == PARTIAL_READ_SHARD_READ) {
    ret = pr_info->shard_names[pr_info->sharded_offset];
    pr_info->sharded_offset = pr_info->sharded_offset + 1;
    query->append(pr_info->partial_read_query.str, pr_info->partial_read_query.length);
    if (pr_info->partial_read_filter.length > 0) {
      query->append(STRING_WITH_LEN(" WHERE "));
      query->append(pr_info->partial_read_filter.str, pr_info->partial_read_filter.length);
    }
    if (pr_info->need_for_update) {
      query->append(STRING_WITH_LEN(" FOR UPDATE"));
    }
    return ret;
  }

  if (pr_info->partial_read_mode == PARTIAL_READ_SHARD_RANGE_READ) {
    //update shard offset if necessary
    if (pr_info->range_offset > pr_info->range_num) {
      pr_info->range_offset = 0;
      pr_info->sharded_offset = pr_info->sharded_offset + 1;
      ret = pr_info->shard_names[pr_info->sharded_offset];
    } else {
      ret = pr_info->shard_names[pr_info->sharded_offset];
    }
  }

  query->append(pr_info->partial_read_query.str, pr_info->partial_read_query.length);
  query->append(STRING_WITH_LEN(" WHERE ("));

  if (pr_info->range_offset == 0) {
    append_ident(query, pr_info->part_col->field_name.str,
                 pr_info->part_col->field_name.length, ident_quote_char);
    // the first range
    query->append(STRING_WITH_LEN(" <= "));
    if (pr_info->part_col->str_needs_quotes()) {
      query->append(STRING_WITH_LEN("'"));
    }
    query->append(pr_info->range_values[pr_info->range_offset]);
    if (pr_info->part_col->str_needs_quotes()) {
      query->append(STRING_WITH_LEN("'"));
    }
  } else if (pr_info->range_offset == pr_info->range_num){
    append_ident(query, pr_info->part_col->field_name.str,
                 pr_info->part_col->field_name.length, ident_quote_char);
    // the last range
    query->append(STRING_WITH_LEN(" > "));
    if (pr_info->part_col->str_needs_quotes()) {
      query->append(STRING_WITH_LEN("'"));
    }
    query->append(pr_info->range_values[pr_info->range_offset -1]);
    if (pr_info->part_col->str_needs_quotes()) {
      query->append(STRING_WITH_LEN("'"));
    }
  } else {
    append_ident(query, pr_info->part_col->field_name.str,
                 pr_info->part_col->field_name.length, ident_quote_char);
    query->append(STRING_WITH_LEN(" > "));
    if (pr_info->part_col->str_needs_quotes()) {
      query->append(STRING_WITH_LEN("'"));
    }
    query->append(pr_info->range_values[pr_info->range_offset-1]);
    if (pr_info->part_col->str_needs_quotes()) {
      query->append(STRING_WITH_LEN("'"));
    }

    query->append(STRING_WITH_LEN(" AND "));
    append_ident(query, pr_info->part_col->field_name.str,
                 pr_info->part_col->field_name.length, ident_quote_char);
    query->append(STRING_WITH_LEN(" <= "));
    if (pr_info->part_col->str_needs_quotes()) {
      query->append(STRING_WITH_LEN("'"));
    }
    query->append(pr_info->range_values[pr_info->range_offset]);
    if (pr_info->part_col->str_needs_quotes()) {
      query->append(STRING_WITH_LEN("'"));
    }
  }

  query->append(STRING_WITH_LEN(")"));
  if (pr_info->partial_read_filter.length > 0) {
    query->append(STRING_WITH_LEN(" AND ("));
    query->append(pr_info->partial_read_filter.str, pr_info->partial_read_filter.length);
    query->append(STRING_WITH_LEN(")"));
  }
  pr_info->range_offset = pr_info->range_offset + 1;
  if (pr_info->need_for_update) {
    query->append(STRING_WITH_LEN(" FOR UPDATE"));
  }

  return ret;

}

int federatedx_io_vitess::actual_query(const char *buffer, size_t length, void *info) {
  return actual_query(buffer, length, info, true);
}

int federatedx_io_vitess::actual_query(const char *buffer, size_t length, void *info, bool init_session_status) {
  int error;
  DBUG_ENTER("federatedx_io_vitess::actual_query");

  if (!mysql.net.vio)
  {
    error = mysql_connect();
    if (error) {
      DBUG_RETURN(error);
    }
  }

  if (init_session_status) {
    if ((error = init_session())) {
      DBUG_RETURN(error);
    }
  }

  partial_read_info *pr_info = NULL;
  if (info != NULL) {
    pr_info = (partial_read_info *)info;
  }

  if (pr_info != NULL) {
    char sql_query_buffer[FEDERATEDX_QUERY_BUFFER_SIZE];
    String sql_query(sql_query_buffer,
                     sizeof(sql_query_buffer),
                     &my_charset_bin);
    sql_query.length(0);

    if (pr_info->partial_read_mode == PARTIAL_READ_SHARD_READ || pr_info->partial_read_mode == PARTIAL_READ_SHARD_RANGE_READ) {
      bool old_reconnect = mysql.reconnect;
      const char *current_db = construct_partial_read_query(&sql_query, pr_info);
      error = mysql_select_db(&mysql, current_db);
      if (error) {
        goto err;
      }
      mysql.reconnect = false;
      in_shard_db = true;

      error = mysql_real_query(&mysql, sql_query.ptr(), sql_query.length());
      mysql.reconnect = old_reconnect;
    } else if (pr_info->partial_read_mode == PARTIAL_READ_RANGE_READ) {

      construct_partial_read_query(&sql_query, pr_info);
      if (in_shard_db) {
        error = mysql_select_db(&mysql, server->database);
        if (error) {
          goto err;
        }
        in_shard_db = false;
      }
      error = mysql_real_query(&mysql, sql_query.ptr(), sql_query.length());

    }
  } else {
    if (in_shard_db) {
      error = mysql_select_db(&mysql, server->database);
      if (error) {
        goto err;
      }
      in_shard_db = false;
    }
    error = mysql_real_query(&mysql, buffer, length);
  }

err:
  DBUG_RETURN(error);
}

int federatedx_io_vitess::init_session()
{
  DBUG_ENTER("federatedx_io_vitess::init_session");
  int error = 0;
  if (mysql.net.thd == NULL) {
    DBUG_RETURN(error);
  }
  st_mysql_const_lex_string key = {"vitess_session", strlen("vitess_session")};
  user_var_entry* var_entry = get_variable(&((THD*)mysql.net.thd)->user_vars, &key, 0);
  if (var_entry == NULL) {
    DBUG_RETURN(error);
  }
  char* session_str = var_entry->value;
  if (session_str == NULL || strlen(session_str) == 0) {
    DBUG_RETURN(error);
  }
  if (strlen(session_str) != strlen(session.str) || memcmp(session_str, session.str, strlen(session.str))) {
    // modified by klkyy2018(fei.long@transwarp.io)
    String set_session_str(1024);
//    char set_session_buffer[1024];
//    String set_session_str(set_session_buffer, sizeof(set_session_buffer), &my_charset_bin);
    // end
    set_session_str.append(STRING_WITH_LEN("set vitess_session='"));
    set_session_str.append(session_str, strlen(session_str));
    set_session_str.append(STRING_WITH_LEN("'"));

    error = actual_query(set_session_str.ptr(), set_session_str.length(), NULL, false);
    if (!error) {
      dynstr_set(&session, 0);
      dynstr_append(&session, session_str);
    }
  }

  DBUG_RETURN(error);
}

