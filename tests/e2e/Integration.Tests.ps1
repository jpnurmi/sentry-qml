#!/usr/bin/env pwsh

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

BeforeAll {
    if ([string]::IsNullOrEmpty($env:SENTRY_APP_RUNNER_PATH)) {
        throw 'SENTRY_APP_RUNNER_PATH must point to a checkout of getsentry/app-runner.'
    }

    . "$env:SENTRY_APP_RUNNER_PATH/import-modules.ps1"

    $script:AppPath = $env:SENTRY_QML_E2E_APP_PATH
    $script:BaseUrl = if ($env:SENTRY_TEST_URL) { $env:SENTRY_TEST_URL } else { 'http://127.0.0.1:9000' }
    $script:RunId = if ($env:SENTRY_QML_E2E_RUN_ID) { $env:SENTRY_QML_E2E_RUN_ID } else { [guid]::NewGuid().ToString() }
    $script:DatabasePath = if ($env:SENTRY_QML_E2E_DATABASE_PATH) {
        $env:SENTRY_QML_E2E_DATABASE_PATH
    } else {
        Join-Path ([System.IO.Path]::GetTempPath()) "sentry-qml-e2e-$($script:RunId)"
    }
    $script:OutputDir = Join-Path $PSScriptRoot 'output'

    if ([string]::IsNullOrEmpty($env:SENTRY_QML_E2E_DSN)) {
        throw 'SENTRY_QML_E2E_DSN must be set.'
    }

    if ([string]::IsNullOrEmpty($env:SENTRY_AUTH_TOKEN)) {
        throw 'SENTRY_AUTH_TOKEN must be set.'
    }

    if ([string]::IsNullOrEmpty($script:AppPath) -or -not (Test-Path $script:AppPath)) {
        throw "SENTRY_QML_E2E_APP_PATH does not point to an executable: $script:AppPath"
    }

    New-Item -ItemType Directory -Path $script:OutputDir -Force | Out-Null
    New-Item -ItemType Directory -Path $script:DatabasePath -Force | Out-Null
    Set-OutputDir -Path $script:OutputDir

    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:SENTRY_QML_E2E_RUN_ID = $script:RunId
    $env:SENTRY_QML_E2E_DATABASE_PATH = $script:DatabasePath

    Connect-SentryApi `
        -ApiToken $env:SENTRY_AUTH_TOKEN `
        -Organization 'sentry' `
        -Project 'internal' `
        -BaseUrl "$($script:BaseUrl)/api/0"

    function script:Get-ObjectValue {
        param(
            [Parameter(Mandatory = $true)]
            $InputObject,

            [Parameter(Mandatory = $true)]
            [string]$Name
        )

        if ($InputObject -is [System.Collections.IDictionary]) {
            return $InputObject[$Name]
        }

        return $InputObject.$Name
    }

    function script:Get-TagValue {
        param(
            [Parameter(Mandatory = $true)]
            $SentryEvent,

            [Parameter(Mandatory = $true)]
            [string]$Key
        )

        foreach ($tag in @(Get-ObjectValue -InputObject $SentryEvent -Name 'tags')) {
            if ((Get-ObjectValue -InputObject $tag -Name 'key') -eq $Key) {
                return Get-ObjectValue -InputObject $tag -Name 'value'
            }
        }

        return $null
    }

    function script:Invoke-E2EAction {
        param(
            [Parameter(Mandatory = $true)]
            [string]$Action,

            [string[]]$AdditionalArgs = @()
        )

        $result = Invoke-DeviceApp -ExecutablePath $script:AppPath -Arguments (@($Action) + $AdditionalArgs)
        $result | ConvertTo-Json -Depth 8 | Out-File -FilePath (Get-OutputFilePath "$Action-result.json")
        return $result
    }

    function script:Invoke-SentryProjectIssues {
        param(
            [Parameter(Mandatory = $true)]
            [string]$Query,

            [int]$Limit = 25
        )

        $queryParameters = @{
            query = $Query
            limit = $Limit
            sort = 'date'
        }
        $queryString = ($queryParameters.GetEnumerator() | ForEach-Object {
            "$($_.Key)=$([System.Web.HttpUtility]::UrlEncode([string]$_.Value))"
        }) -join '&'
        $uri = "$($script:BaseUrl)/api/0/projects/sentry/internal/issues/?$queryString"

        $response = Invoke-WebRequest `
            -Uri $uri `
            -Method 'GET' `
            -Headers @{ Authorization = "Bearer $env:SENTRY_AUTH_TOKEN" } `
            -ContentType 'application/json'

        return , @($response.Content | ConvertFrom-Json -AsHashtable)
    }

    function script:Get-SentryTestFeedbackIssue {
        param(
            [Parameter(Mandatory = $true)]
            [string]$Message,

            [int]$TimeoutSeconds = 180
        )

        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        $lastError = $null
        do {
            try {
                $issues = Invoke-SentryProjectIssues -Query 'issue.category:feedback'
                foreach ($issue in @($issues)) {
                    $issueJson = $issue | ConvertTo-Json -Depth 16 -Compress
                    if ($issueJson.Contains($Message)) {
                        return $issue
                    }
                }
            } catch {
                $lastError = $_
            }

            Start-Sleep -Seconds 5
        } while ([DateTime]::UtcNow -lt $deadline)

        if ($lastError) {
            throw "Feedback issue '$Message' was not found. Last API error: $lastError"
        }
        throw "Feedback issue '$Message' was not found within $TimeoutSeconds seconds."
    }
}

AfterAll {
    if (Get-Command Disconnect-SentryApi -ErrorAction SilentlyContinue) {
        Disconnect-SentryApi
    }
}

Describe 'Sentry QML E2E' {
    BeforeAll {
        Connect-Device -Platform 'Linux'
    }

    AfterAll {
        if (Get-Command Disconnect-Device -ErrorAction SilentlyContinue) {
            Disconnect-Device
        }
    }

    Context 'Message capture' {
        BeforeAll {
            $script:MessageResult = Invoke-E2EAction -Action 'message-capture'
            $script:MessageEventIds = Get-EventIds -AppOutput $script:MessageResult.Output -ExpectedCount 1
            $script:MessageEvent = Get-SentryTestEvent -EventId $script:MessageEventIds[0] -TimeoutSeconds 180
        }

        It 'exits cleanly' {
            $script:MessageResult.ExitCode | Should -Be 0
        }

        It 'captures a message event in Sentry' {
            $script:MessageEvent | Should -Not -BeNullOrEmpty
            Get-ObjectValue -InputObject $script:MessageEvent -Name 'title' | Should -Be "Sentry QML E2E $script:RunId"
        }

        It 'keeps the QML correlation tags' {
            Get-TagValue -SentryEvent $script:MessageEvent -Key 'e2e_run_id' | Should -Be $script:RunId
            Get-TagValue -SentryEvent $script:MessageEvent -Key 'test.action' | Should -Be 'message-capture'
        }
    }

    Context 'Feedback capture' {
        BeforeAll {
            $script:FeedbackMessage = "Sentry QML E2E feedback $script:RunId"
            $script:FeedbackEventMessage = "Sentry QML E2E feedback event $script:RunId"
            $script:FeedbackResult = Invoke-E2EAction -Action 'feedback-capture'
            $script:FeedbackEventIds = Get-EventIds -AppOutput $script:FeedbackResult.Output -ExpectedCount 1
            $script:FeedbackEvent = Get-SentryTestEvent -EventId $script:FeedbackEventIds[0] -TimeoutSeconds 180
            $script:FeedbackIssue = Get-SentryTestFeedbackIssue `
                -Message $script:FeedbackMessage `
                -TimeoutSeconds 180
        }

        It 'exits cleanly' {
            $script:FeedbackResult.ExitCode | Should -Be 0
        }

        It 'captures the associated message event in Sentry' {
            $script:FeedbackEvent | Should -Not -BeNullOrEmpty
            Get-ObjectValue -InputObject $script:FeedbackEvent -Name 'title' | Should -Be $script:FeedbackEventMessage
            Get-TagValue -SentryEvent $script:FeedbackEvent -Key 'e2e_run_id' | Should -Be $script:RunId
            Get-TagValue -SentryEvent $script:FeedbackEvent -Key 'test.action' | Should -Be 'feedback-capture'
        }

        It 'captures feedback as a separate Sentry issue' {
            $script:FeedbackIssue | Should -Not -BeNullOrEmpty
            $feedbackJson = $script:FeedbackIssue | ConvertTo-Json -Depth 16 -Compress
            $feedbackJson.Contains($script:FeedbackMessage) | Should -BeTrue
        }
    }

    Context 'Crash capture' {
        BeforeAll {
            $script:CrashId = [guid]::NewGuid().ToString()
            $script:CrashResult = Invoke-E2EAction -Action 'crash-capture' -AdditionalArgs @($script:CrashId)
            $script:CrashSendResult = Invoke-E2EAction -Action 'crash-send'
            $script:CrashEvent = Get-SentryTestEvent -TagName 'test.crash_id' -TagValue $script:CrashId -TimeoutSeconds 300
        }

        It 'crashes the app process' {
            $script:CrashResult.ExitCode | Should -Not -Be 0
        }

        It 'prints the crash correlation id' {
            $script:CrashResult.Output | Where-Object { $_ -eq "CRASH_ID: $script:CrashId" } | Should -Not -BeNullOrEmpty
        }

        It 'can relaunch to flush pending crash data' {
            $script:CrashSendResult.ExitCode | Should -Be 0
        }

        It 'captures a crash event in Sentry' {
            $script:CrashEvent | Should -Not -BeNullOrEmpty
            Get-ObjectValue -InputObject $script:CrashEvent -Name 'type' | Should -Be 'error'
        }

        It 'keeps the QML crash context' {
            Get-TagValue -SentryEvent $script:CrashEvent -Key 'e2e_run_id' | Should -Be $script:RunId
            Get-TagValue -SentryEvent $script:CrashEvent -Key 'test.action' | Should -Be 'crash-capture'
            Get-TagValue -SentryEvent $script:CrashEvent -Key 'test.crash_id' | Should -Be $script:CrashId
        }

        It 'contains exception information' {
            $exception = Get-ObjectValue -InputObject $script:CrashEvent -Name 'exception'
            $exception | Should -Not -BeNullOrEmpty
            (Get-ObjectValue -InputObject $exception -Name 'values') | Should -Not -BeNullOrEmpty
        }
    }
}
