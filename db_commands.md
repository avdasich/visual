# подключиться
psql -d phone_monitor

# если не коннектится
psql -h localhost -p 5432 -U m1 -d phone_monitor

-- нормальный вывод чтоб json не ломал таблицу
\x

-- посмотреть таблицы
\dt

-- структура таблицы
\d telemetry

-- последние записи

SELECT id, received,
  raw_data->'location'->>'latitude'  AS latitude,
  raw_data->'location'->>'longitude' AS longitude,
  raw_data->'location'->>'altitude'  AS altitude,
  raw_data->'location'->>'accuracy'  AS accuracy,
  raw_data->'location'->>'time'      AS gps_time
FROM telemetry
ORDER BY id DESC
LIMIT 10;

-- 10 колонок 
SELECT id, received, lat, lon, alt, lte_pci, lte_rsrp, gsm_dbm
FROM telemetry
LIMIT 10;


-- самая последняя запись
SELECT * FROM telemetry
ORDER BY id DESC
LIMIT 1;

-- перенести координаты из json в колонки
UPDATE telemetry
SET
  lat      = (raw_data->'location'->>'latitude')::double precision,
  lon      = (raw_data->'location'->>'longitude')::double precision,
  alt      = (raw_data->'location'->>'altitude')::real,
  accuracy = (raw_data->'location'->>'accuracy')::real,
  gps_time = (raw_data->'location'->>'time')::bigint
WHERE raw_data ? 'location'
AND raw_data->'location'->>'latitude' IS NOT NULL
AND raw_data->'location'->>'longitude' IS NOT NULL;

-- gsm из json
SELECT id, received,
  raw_data->'gsm'->0->>'ci'  AS gsm_ci,
  raw_data->'gsm'->0->>'dbm' AS gsm_dbm,
  raw_data->'gsm'->0->>'mcc' AS mcc,
  raw_data->'gsm'->0->>'mnc' AS mnc
FROM telemetry
ORDER BY id DESC
LIMIT 10;

-- lte из json
SELECT id, received,
  raw_data->'lte'->0->>'pci'    AS lte_pci,
  raw_data->'lte'->0->>'rsrp'   AS lte_rsrp,
  raw_data->'lte'->0->>'rsrq'   AS lte_rsrq,
  raw_data->'lte'->0->>'rssi'   AS lte_rssi,
  raw_data->'lte'->0->>'earfcn' AS earfcn
FROM telemetry
WHERE raw_data ? 'lte'
ORDER BY id DESC
LIMIT 10;

-- только записи с нормальными координатами
SELECT id, received, lat, lon, alt, accuracy,
       gsm_ci, gsm_dbm, lte_pci, lte_rsrp
FROM telemetry
WHERE lat <> 0 AND lon <> 0
ORDER BY id DESC
LIMIT 20;

-- средний rsrp по каждому pci
SELECT lte_pci, AVG(lte_rsrp) AS avg_rsrp, COUNT(*) AS count
FROM telemetry
WHERE lte_rsrp <> 0
GROUP BY lte_pci
ORDER BY avg_rsrp DESC;


-- сколько всего записей
SELECT COUNT(*) FROM telemetry;

-- выйти
\q

