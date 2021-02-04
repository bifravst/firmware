Feature: Connect

    The Cat Tracker should connect to the AWS IoT broker

    Background:
        
        Given I am run after the "Run the firmware" feature
        Given I connect the cat tracker {env__JOB_ID}
        Given I am authenticated with AWS key "{env__AWS_ACCESS_KEY_ID}" and secret "{env__AWS_SECRET_ACCESS_KEY}"

    Scenario: Read reported state

        When I execute "getThingShadow" of the AWS IotData SDK with
        """
        {
            "thingName": "{env__JOB_ID}"
        }
        """
        And I parse "awsSdk.res.payload" into "shadow"
        Then "shadow.state.reported" should match this JSON
        """
        {
            "dev": {
                "v": {
                    "modV": "mfw_nrf9160_1.2.2",
                    "brdV": "nrf9160dk_nrf9160",
                    "appV": "{env__CAT_TRACKER_APP_VERSION}-upgraded"
                }
            },
            "cfg": {
                "gpst": 60,
                "act": true,
                "actwt": 60,
                "mvres": 60,
                "mvt": 3600,
                "acct": 19.6133
            }
        }
        """