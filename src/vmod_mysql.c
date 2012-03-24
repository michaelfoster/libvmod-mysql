#include <stdbool.h>
#include <stdlib.h>
#include <mysql/mysql.h>

#include "bin/varnishd/cache.h"

#include "vcc_if.h"

struct {
	MYSQL handle;
} vmod_mysql;

struct vmod_mysql_res {
	unsigned	magic;
#define VMOD_MYSQL_MAGIC 0xBBB0C87C
	MYSQL_RES *result;
	MYSQL_ROW row;
	
	unsigned xid;
	unsigned int num_fields;
	MYSQL_FIELD *fields;
};
static struct vmod_mysql_res **vmod_mysql_list;
int vmod_mysql_list_sz;
static pthread_mutex_t cl_mtx = PTHREAD_MUTEX_INITIALIZER;

static void cm_clear(struct vmod_mysql_res *c);

static void cm_init(struct vmod_mysql_res *c) {
	c->magic = VMOD_MYSQL_MAGIC;
	
	cm_clear(c);
}

static void cm_clear(struct vmod_mysql_res *c) {
	CHECK_OBJ_NOTNULL(c, VMOD_MYSQL_MAGIC);
	
	c->result = NULL;
	c->row = NULL;
	c->fields = NULL;
	c->num_fields = 0;
}

static struct vmod_mysql_res* cm_get(struct sess *sp) {
	struct vmod_mysql_res *cm;
	AZ(pthread_mutex_lock(&cl_mtx));

	while (vmod_mysql_list_sz <= sp->id) {
		int ns = vmod_mysql_list_sz*2;
		/* resize array */
		vmod_mysql_list = realloc(vmod_mysql_list, ns * sizeof(struct vmod_mysql_res *));
		for (; vmod_mysql_list_sz < ns; vmod_mysql_list_sz++) {
			vmod_mysql_list[vmod_mysql_list_sz] = malloc(sizeof(struct vmod_mysql_res));
			cm_init(vmod_mysql_list[vmod_mysql_list_sz]);
		}
		assert(vmod_mysql_list_sz == ns);
		AN(vmod_mysql_list);
	}
	cm = vmod_mysql_list[sp->id];
	if (cm->xid != sp->xid) {
		cm_clear(cm);
		cm->xid = sp->xid;
	}
	AZ(pthread_mutex_unlock(&cl_mtx));
	return cm;
}

const char *
vmod_error(struct sess *sp)
{
	return mysql_error(&vmod_mysql.handle);
}

const char *
vmod_col(struct sess *sp, const char *col_name)
{
	struct vmod_mysql_res *r;
	int i;
	
	r = cm_get(sp);

	for(i = 0; i < r->num_fields; i++) {
		if(strcmp(r->fields[i].name, col_name) == 0) {
			return r->row[i];
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
	if(i < 0 || i >= r->num_fields) {
		/* Column out of range */
		return NULL;
	}

	return r->row[i];
}

unsigned
vmod_fetch(struct sess *sp)
{
	struct vmod_mysql_res *r;
	
	r = cm_get(sp);
	
	if(r->result == NULL) {
		/* No result */
		return false;
	}

	r->row = mysql_fetch_row(r->result);

	return r->row != NULL;
}

void
vmod_free_result(struct sess *sp)
{
	struct vmod_mysql_res *r;
	
	r = cm_get(sp);
	
	if(r->result != NULL) {
		mysql_free_result(r->result);
	}
	
	cm_clear(r);
}

unsigned
vmod_query(struct sess *sp, const char *query)
{
	struct vmod_mysql_res *r;
	
	r = cm_get(sp);
	
	if(mysql_query(&vmod_mysql.handle, query) == 0) {
		/* Query succeeded */
		r->result = mysql_store_result(&vmod_mysql.handle);
		if(r->result != NULL) {
			r->num_fields = mysql_num_fields(r->result);
			r->fields = mysql_fetch_fields(r->result);
		}
		return true;
	} else {
		/* Query failed */
		r->result = NULL;
		return false;
	}
}

int
vmod_num_rows(struct sess *sp)
{
	struct vmod_mysql_res *r;
	
	r = cm_get(sp);
	
	if(r->result == NULL) {
		/* Statement returned no result */
		return 0;
	}
	return mysql_num_rows(r->result);
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
	return mysql_real_connect(&vmod_mysql.handle, host, user, password, database, 0, NULL, 0) != NULL;
}

int
init_function(struct vmod_priv *priv, const struct VCL_conf *conf)
{
	int i;
	
	mysql_init(&vmod_mysql.handle);
	
	vmod_mysql_list_sz = 256;
	vmod_mysql_list = malloc(sizeof(struct vmod_mysql_res *) * vmod_mysql_list_sz);
	AN(vmod_mysql_list);
	
	for(i = 0; i < vmod_mysql_list_sz; i++) {
		vmod_mysql_list[i] = malloc(sizeof(struct vmod_mysql_res));
		cm_init(vmod_mysql_list[i]);
	}
	
	return 0;
}

