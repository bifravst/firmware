@Last
Feature: Delete the Cat Tracker

  This deletes the test device

  Background:

    Given I am authenticated with AWS key "{awsAccessKeyId}" and secret "{awsSecretAccessKey}"

  Scenario: Delete the cat

    When I execute "listThingPrincipals" of the AWS Iot SDK with
      """
      {
        "thingName": "{jobId}"
      }
      """
    Then "$count(awsSdk.res.principals)" should equal 1
    Given I store "awsSdk.res.principals[0]" into "certificateArn"
    Given I store "$split(awsSdk.res.principals[0], '/')[1]" into "certificateId"
    Given I execute "detachThingPrincipal" of the AWS Iot SDK with
      """
      {
        "thingName": "{jobId}",
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
        "thingName": "{jobId}"
      }
      """
