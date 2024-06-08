MODULES = nats_notify
EXTENSION = nats_notify
DATA = sql/nats_notify--1.0.sql

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

SHLIB_LINK = -lnats