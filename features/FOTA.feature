Feature: Update the firmware

    The Cat Tracker should have updated the firmware

    Scenario: The updated firmware should have been run

        Given the Firmware CI job "{env__JOB_ID}" has completed
        Then the Firmware CI device log for job "{env__JOB_ID}" should contain
        """
        cat_tracker:  Version:     {env__CAT_TRACKER_APP_VERSION}-updated
        """