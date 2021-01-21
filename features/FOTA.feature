Feature: Upgrade the firmware

    The Cat Tracker should have upgraded the firmware

    Scenario: The upgraded firmware should have been run

        Given the Firmware CI job "{env__JOB_ID}" has completed
        Then the Firmware CI device log for job "{env__JOB_ID}" should contain
        """
        cat_tracker:  Version:     {env__APP_VERSION}-upgraded
        """