Feature: Connect

    The Cat Tracker should connect to the AWS IoT broker

    Background:
        
        Given I connect the cat tracker {env.JOB_ID}
        Given I am authenticated with AWS key "{env.TESTENV_AWS_ACCESS_KEY_ID}" and secret "{env.TESTENV_AWS_SECRET_ACCESS_KEY}"

    Scenario: Read reported and desired state

        When I execute "getThingShadow" of the AWS IotData SDK with
        """
        {
            "thingName": "{env.JOB_ID}"
        }
        """
        And I parse "awsSdk.res.payload" into "shadow"
        Then "shadow.state.reported" should match this JSON
        """
        {
            "dev": {
                "v": {
                    "modV": "mfw_nrf9160_1.2.0",
                    "brdV": "thingy91_nrf9160",
                    "appV": "{env.NEXT_VERSION}"
                }
            }
        }
        """
        And "shadow.state.desired" should match this JSON
        """
        {
            "cfg": {
                "act": false,
                "actwt": 60,
                "mvres": 60,
                "mvt": 3600,
                "gpst": 1000,
                "celt": 600,
                "acct": 5
            }
        }
        """