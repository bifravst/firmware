@Last
Feature: Delete the Cat Tracker

  This deletes the test device

  Background:

    Given I am authenticated with AWS key "{env__AWS_ACCESS_KEY_ID}" and secret "{env__AWS_SECRET_ACCESS_KEY}"

  Scenario: Delete the cat

    When I execute "listThingPrincipals" of the AWS Iot SDK with
      """
      {
        "thingName": "{env__JOB_ID}"
      }
      """
    Then "$count(awsSdk.res.principals)" should equal 1
    Given I store "awsSdk.res.principals[0]" into "certificateArn"
    Given I store "$split(awsSdk.res.principals[0], '/')[1]" into "certificateId"
    Given I execute "detachThingPrincipal" of the AWS Iot SDK with
      """
      {
        "thingName": "{env__JOB_ID}",
        "principal": "{certificateArn}"
      }
      """
    And I execute "updateCertificate" of the AWS Iot SDK with
      """
      {
        "certificateId": "{certificateId}",
        "newStatus": "INACTIVE"
      }
      """
    And I execute "deleteCertificate" of the AWS Iot SDK with
      """
      {
        "certificateId": "{certificateId}"
      }
      """
    And I execute "deleteThing" of the AWS Iot SDK with
      """
      {
        "thingName": "{env__JOB_ID}"
      }
      """
