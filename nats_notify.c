#include "postgres.h"
#include "fmgr.h"
#include "libpq-fe.h"
#include "executor/spi.h"
#include "commands/trigger.h"
#include "utils/jsonb.h"
#include "utils/guc.h"
#include "utils/tuplestore.h"
#include "access/xact.h" // Inclusion correcte de l'en-tête xact.h pour XactEvent

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

void _PG_init(void);
void _PG_fini(void);

static PGconn *conn = NULL;
static List *notifications = NIL;

static char *nats_url = NULL;

static void nats_commit_callback(XactEvent event, void *arg);

PG_FUNCTION_INFO_V1(nats_notify_trigger);

Datum nats_notify_trigger(PG_FUNCTION_ARGS);

void _PG_init(void)
{
    DefineCustomStringVariable(
        "nats_notify.url",
        "URL of the NATS server.",
        NULL,
        &nats_url,
        "nats://localhost:4222",
        PGC_SUSET,
        GUC_SUPERUSER_ONLY,
        NULL,
        NULL,
        NULL
    );

    if (nats_url != NULL)
    {
        conn = PQconnectdb(nats_url);
        if (PQstatus(conn) != CONNECTION_OK)
            ereport(ERROR, (errmsg("Failed to connect to NATS server: %s", PQerrorMessage(conn))));
    }

    RegisterXactCallback(nats_commit_callback, NULL);
}

void _PG_fini(void)
{
    if (conn != NULL)
        PQfinish(conn);
    UnregisterXactCallback(nats_commit_callback, NULL);
}

Datum nats_notify_trigger(PG_FUNCTION_ARGS)
{
    TriggerData *trigdata;
    HeapTuple new_row;
    TupleDesc tupdesc;
    char *table_name;
    char *data;
    JsonbValue jsonb_val;
    JsonbParseState *state = NULL;
    Jsonb *jsonb;
    char *json_str;

    trigdata = (TriggerData *) fcinfo->context;

    if (!CALLED_AS_TRIGGER(fcinfo))
        elog(ERROR, "not called by trigger manager");

    if (!TRIGGER_FIRED_BY_INSERT(trigdata->tg_event) && !TRIGGER_FIRED_BY_UPDATE(trigdata->tg_event))
        elog(ERROR, "not fired by insert or update");

    new_row = trigdata->tg_newtuple;
    tupdesc = RelationGetDescr(trigdata->tg_relation); // Changed to use RelationGetDescr
    table_name = SPI_getrelname(trigdata->tg_relation);
    data = SPI_getvalue(new_row, tupdesc, 1); // Assuming data is in the first column

    // Preparation for JSONB
    pushJsonbValue(&state, WJB_BEGIN_OBJECT, NULL);

    // Adding table name
    jsonb_val.type = jbvString;
    jsonb_val.val.string.len = strlen("table");
    jsonb_val.val.string.val = "table";
    pushJsonbValue(&state, WJB_KEY, &jsonb_val);

    jsonb_val.type = jbvString;
    jsonb_val.val.string.len = strlen(table_name);
    jsonb_val.val.string.val = table_name;
    pushJsonbValue(&state, WJB_VALUE, &jsonb_val);

    // Adding row data
    jsonb_val.type = jbvString;
    jsonb_val.val.string.len = strlen("data");
    jsonb_val.val.string.val = "data";
    pushJsonbValue(&state, WJB_KEY, &jsonb_val);

    jsonb_val.type = jbvString;
    jsonb_val.val.string.len = strlen(data);
    jsonb_val.val.string.val = data;
    pushJsonbValue(&state, WJB_VALUE, &jsonb_val);

    pushJsonbValue(&state, WJB_END_OBJECT, NULL);

    jsonb = JsonbValueToJsonb(pushJsonbValue(&state, WJB_END_OBJECT, NULL));

    json_str = JsonbToCString(NULL, &jsonb->root, VARSIZE(jsonb));
    notifications = lappend(notifications, pstrdup(json_str));

    PG_RETURN_POINTER(new_row);
}

static void nats_commit_callback(XactEvent event, void *arg)
{
    ListCell *lc;
    if (event == XACT_EVENT_COMMIT && notifications != NIL)
    {
        foreach(lc, notifications)
        {
            char *data = (char *) lfirst(lc);
            PGresult *res = PQexec(conn, "my_channel", data);
            if (PQresultStatus(res) != PGRES_COMMAND_OK)
                ereport(WARNING, (errmsg("Failed to publish to NATS: %s", PQresultErrorMessage(res))));
            PQclear(res);
        }

        list_free_deep(notifications);
        notifications = NIL;
    }
    else if (event == XACT_EVENT_ABORT)
    {
        list_free_deep(notifications);
        notifications = NIL;
    }
}
