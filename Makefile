EXTENSION = nats_notify
MODULE_big = nats_notify
OBJS = nats_notify.o

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

override CFLAGS += -I/usr/include/postgresql -I/usr/include/postgresql/13/server -I/usr/local/include
override LDFLAGS += -L/usr/local/lib -lnats
