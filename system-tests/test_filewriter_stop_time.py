from helpers.nexushelpers import OpenNexusFileWhenAvailable
from helpers.kafkahelpers import (
    create_producer,
    publish_run_start_message,
    publish_run_stop_message,
    consume_everything,
    publish_f142_message,
)
from helpers.timehelpers import unix_time_milliseconds
from time import sleep
from datetime import datetime
import json
from streaming_data_types.fbschemas.logdata_f142.AlarmStatus import AlarmStatus
from streaming_data_types.fbschemas.logdata_f142.AlarmSeverity import AlarmSeverity


def test_filewriter_clears_stop_time_between_jobs(docker_compose_stop_command):
    producer = create_producer()
    sleep(10)

    # Ensure TEST_sampleEnv topic exists
    publish_f142_message(
        producer, "TEST_sampleEnv", int(unix_time_milliseconds(datetime.utcnow()))
    )

    topic = "TEST_writerCommand"
    publish_run_start_message(
        producer,
        "commands/nexus_structure.json",
        "output_file_with_stop_time.nxs",
        topic=topic,
        job_id="should_start_then_stop",
        stop_time=int(unix_time_milliseconds(datetime.utcnow())),
    )
    sleep(10)
    job_id = publish_run_start_message(
        producer,
        "commands/nexus_structure.json",
        "output_file_no_stop_time.nxs",
        topic=topic,
        job_id="should_start_but_not_stop",
    )
    sleep(10)
    msgs = consume_everything("TEST_writerStatus")

    stopped = False
    started = False
    for message in msgs:
        message = json.loads(str(message.value(), encoding="utf-8"))
        if message["start_time"] > 0 and message["job_id"] == job_id:
            started = True
        if message["stop_time"] > 0 and message["job_id"] == job_id:
            stopped = True

    assert started
    assert not stopped

    # Clean up by stopping writing
    publish_run_stop_message(producer, job_id=job_id)
    sleep(10)


def test_filewriter_can_write_data_when_start_and_stop_time_are_in_the_past(
    docker_compose_stop_command,
):
    producer = create_producer()

    sleep(5)
    data_topics = ["TEST_historicalData1", "TEST_historicalData2"]

    first_alarm_change_time_ms = 1_560_330_000_050
    second_alarm_change_time_ms = 1_560_330_000_060

    # Publish some data with timestamps in the past(these are from 2019 - 06 - 12)
    for data_topic in data_topics:
        for time_in_ms_after_epoch in range(1_560_330_000_000, 1_560_330_000_200):
            if time_in_ms_after_epoch == first_alarm_change_time_ms:
                # EPICS alarm goes into HIGH state
                publish_f142_message(
                    producer,
                    data_topic,
                    time_in_ms_after_epoch,
                    alarm_status=AlarmStatus.HIGH,
                    alarm_severity=AlarmSeverity.MAJOR,
                )
            elif time_in_ms_after_epoch == second_alarm_change_time_ms:
                # EPICS alarm returns to NO_ALARM
                publish_f142_message(
                    producer,
                    data_topic,
                    time_in_ms_after_epoch,
                    alarm_status=AlarmStatus.NO_ALARM,
                    alarm_severity=AlarmSeverity.NO_ALARM,
                )
            else:
                publish_f142_message(producer, data_topic, time_in_ms_after_epoch)

    sleep(5)

    command_topic = "TEST_writerCommand"
    start_time = 1_560_330_000_002
    stop_time = 1_560_330_000_148
    # Ask to write 147 messages from the middle of the 200 messages we published
    publish_run_start_message(
        producer,
        "commands/nexus_structure_historical.json",
        "output_file_of_historical_data.nxs",
        start_time=start_time,
        stop_time=stop_time,
        topic=command_topic,
    )
    # The command also includes a stream for topic TEST_emptyTopic which exists but has no data in it, the
    # file writer should recognise there is no data in that topic and close the corresponding streamer without problem.
    filepath = "output-files/output_file_of_historical_data.nxs"
    with OpenNexusFileWhenAvailable(filepath) as file:
        # Expect to have recorded one value per ms between the start and stop time
        # +1 because time range for file writer is inclusive
        assert file["entry/historical_data_1/time"].len() == (
            stop_time - start_time + 1
        ), "Expected there to be one message per millisecond recorded between specified start and stop time"
        assert file["entry/historical_data_2/time"].len() == (
            stop_time - start_time + 1
        ), "Expected there to be one message per millisecond recorded between specified start and stop time"

        # EPICS alarms
        assert (
            file["entry/historical_data_1/alarm_status"].len() == 2
        ), "Expected there to have record two changes in EPICS alarm status"
        assert (
            file["entry/historical_data_1/alarm_severity"].len() == 2
        ), "Expected there to have record two changes in EPICS alarm status"
        # First alarm change
        assert file["entry/historical_data_1/alarm_status"][0] == "HIGH"
        assert file["entry/historical_data_1/alarm_severity"][0] == "MAJOR"
        assert (
            file["entry/historical_data_1/alarm_time"][0]
            == first_alarm_change_time_ms * 1000
        )  # ns
        # Second alarm change
        assert file["entry/historical_data_1/alarm_status"][1] == "NO_ALARM"
        assert file["entry/historical_data_1/alarm_severity"][1] == "NO_ALARM"
        assert (
            file["entry/historical_data_1/alarm_time"][1]
            == second_alarm_change_time_ms * 1000
        )  # ns

        assert (
            file["entry/no_data/time"].len() == 0
        ), "Expect there to be no data as the source topic is empty"
