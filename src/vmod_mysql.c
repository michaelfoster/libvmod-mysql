#include <stdbool.h>
#include <stdlib.h>
#include <mysql/mysql.h>

#include "vrt.h"
#include "bin/varnishd/cache.h"

#include "vcc_if.h"

struct {
	MYSQL handle;
	MYSQL_RES *result;

	MYSQL_ROW row;

	unsigned int num_fields;
	MYSQL_FIELD *fields;
} vmod_mysql;

const char *
vmod_error(struct sess *sp)
{
	return mysql_error(&vmod_mysql.handle);
}

const char *
vmod_col(struct sess *sp, const char *col_name)
{
	int i;

	for(i = 0; i < vmod_mysql.num_fields; i++) {
		if(strcmp(vmod_mysql.fields[i].name, col_name) == 0) {
			return vmod_mysql.row[i];
		}
	}

	/* Check if maybe it was a number instead */
	for(i = 0; i < strlen(col_name); i++) {
		if(col_name[i] < '0' || col_name[i] > '9') {
			/* Nope */
			return NULL;
		}
	}

	i = atoi(col_name);
	if(i < 0 || i >= vmod_mysql.num_fields) {
		/* Column out of range */
		return NULL;
	}

	return vmod_mysql.row[i];
}

unsigned
vmod_fetch(struct sess *sp)
{
	if(vmod_mysql.result == NULL) {
		/* No result */
		return false;
	}

	vmod_mysql.row = mysql_fetch_row(vmod_mysql.result);

	return vmod_mysql.row != NULL;
}

void
vmod_free_result(struct sess *sp)
{
	if(vmod_mysql.result != NULL) {
		mysql_free_result(vmod_mysql.result);
	}
}

unsigned
vmod_query(struct sess *sp, const char *query)
{
	if(mysql_query(&vmod_mysql.handle, query) == 0) {
		/* Query succeeded */
		vmod_mysql.result = mysql_store_result(&vmod_mysql.handle);

		vmod_mysql.num_fields = mysql_num_fields(vmod_mysql.result);
		vmod_mysql.fields = mysql_fetch_fields(vmod_mysql.result);

		return true;
	} else {
		/* Query failed */
		vmod_mysql.result = NULL;
		return false;
	}
}

int
vmod_num_rows(struct sess *sp)
{
	return mysql_num_rows(&vmod_mysql.handle);
}

int
vmod_affected_rows(struct sess *sp)
{
	return mysql_affected_rows(&vmod_mysql.handle);
}

const char *
vmod_escape(struct sess *sp, const char *string) {
	char *escaped;
	unsigned long len;

	escaped = malloc(strlen(string) * 2 + 3);
	escaped[0] = '\'';

	len = mysql_real_escape_string(&vmod_mysql.handle, escaped + 1, string, strlen(string));

	escaped[len+1] = '\'';
	escaped[len+2] = 0;

	return escaped;
}

unsigned
vmod_connect(struct sess *sp, const char *host, const char *user, const char *password, const char *database)
{
	return mysql_real_connect(&vmod_mysql.handle, host, user, password, database, 0, NULL, 0);
}

int
init_function(struct vmod_priv *priv, const struct VCL_conf *conf)
{
	mysql_init(&vmod_mysql.handle);

	return 0;
}
