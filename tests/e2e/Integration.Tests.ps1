#!/usr/bin/env pwsh

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$IsCocoa = $env:SENTRY_QML_E2E_SDK -eq 'Cocoa'

BeforeAll {
    function Add-AndroidBuildToolsToPath {
        if ((Get-Command aapt -ErrorAction SilentlyContinue) -or (Get-Command aapt2 -ErrorAction SilentlyContinue)) {
            return
        }

        $androidSdkCandidates = @(
            $env:ANDROID_HOME,
            $env:ANDROID_SDK_ROOT,
            '/usr/local/lib/android/sdk'
        ) | Where-Object { -not [string]::IsNullOrEmpty($_) } | Select-Object -Unique

        foreach ($androidSdk in $androidSdkCandidates) {
            $buildToolsRoot = Join-Path $androidSdk 'build-tools'
            $buildToolsDir = Get-ChildItem $buildToolsRoot -Directory -ErrorAction SilentlyContinue |
                Where-Object {
                    (Test-Path (Join-Path $_.FullName 'aapt')) -or
                    (Test-Path (Join-Path $_.FullName 'aapt2'))
                } |
                Select-Object -First 1

            if ($buildToolsDir) {
                $env:PATH = "$($buildToolsDir.FullName)$([System.IO.Path]::PathSeparator)$env:PATH"
                return
            }
        }

        throw 'aapt or aapt2 not found in PATH or Android SDK Build Tools.'
    }

    if ([string]::IsNullOrEmpty($env:SENTRY_APP_RUNNER_PATH)) {
        throw 'SENTRY_APP_RUNNER_PATH must point to a checkout of getsentry/app-runner.'
    }

    . "$env:SENTRY_APP_RUNNER_PATH/import-modules.ps1"

    $script:AppPath = $env:SENTRY_QML_E2E_APP_PATH
    $script:AppPackagePath = $env:SENTRY_QML_E2E_APP_PACKAGE_PATH
    $script:DevicePlatform = if ($env:SENTRY_QML_E2E_PLATFORM) {
        $env:SENTRY_QML_E2E_PLATFORM
    } else {
        'Local'
    }
    $script:IsAndroid = $script:DevicePlatform -eq 'Adb'
    $script:IsWasmBrowser = $script:DevicePlatform -eq 'WasmBrowser'
    if ($script:IsAndroid) {
        Add-AndroidBuildToolsToPath
    }
    $script:BaseUrl = if ($env:SENTRY_TEST_URL) { $env:SENTRY_TEST_URL.TrimEnd([char]'/') } else { 'https://sentry.io' }
    $script:SentryOrg = $env:SENTRY_ORG
    $script:SentryProject = $env:SENTRY_PROJECT
    $script:SentryOrgPath = if ($script:SentryOrg) { [System.Uri]::EscapeDataString($script:SentryOrg) } else { $null }
    $script:SentryProjectPath = if ($script:SentryProject) { [System.Uri]::EscapeDataString($script:SentryProject) } else { $null }
    $script:RunId = if ($env:SENTRY_QML_E2E_RUN_ID) { $env:SENTRY_QML_E2E_RUN_ID } else { [guid]::NewGuid().ToString() }
    $script:DatabasePath = if ($env:SENTRY_QML_E2E_DATABASE_PATH) {
        $env:SENTRY_QML_E2E_DATABASE_PATH
    } else {
        Join-Path ([System.IO.Path]::GetTempPath()) "sentry-qml-e2e-$($script:RunId)"
    }
    $script:AppDsn = if ($env:SENTRY_QML_E2E_APP_DSN) {
        $env:SENTRY_QML_E2E_APP_DSN
    } else {
        $env:SENTRY_QML_E2E_DSN
    }
    if ($script:IsAndroid) {
        $script:AppDsn = $script:AppDsn -replace '127\.0\.0\.1', '10.0.2.2'
        $script:AppDsn = $script:AppDsn -replace 'localhost', '10.0.2.2'
    }
    $script:OutputDir = Join-Path $PSScriptRoot 'output'

    if ([string]::IsNullOrEmpty($env:SENTRY_QML_E2E_DSN)) {
        throw 'SENTRY_QML_E2E_DSN must be set.'
    }

    if ([string]::IsNullOrEmpty($env:SENTRY_AUTH_TOKEN)) {
        throw 'SENTRY_AUTH_TOKEN must be set.'
    }

    if ([string]::IsNullOrEmpty($script:SentryOrg)) {
        throw 'SENTRY_ORG must be set.'
    }

    if ([string]::IsNullOrEmpty($script:SentryProject)) {
        throw 'SENTRY_PROJECT must be set.'
    }

    if ($script:IsAndroid) {
        if ([string]::IsNullOrEmpty($script:AppPackagePath) -or -not (Test-Path $script:AppPackagePath)) {
            throw "SENTRY_QML_E2E_APP_PACKAGE_PATH does not point to an APK: $script:AppPackagePath"
        }
        if ([string]::IsNullOrEmpty($script:AppPath)) {
            throw 'SENTRY_QML_E2E_APP_PATH must be set to the Android package/activity path.'
        }
    } elseif ($script:IsWasmBrowser) {
        if ([string]::IsNullOrEmpty($script:AppPath) -or -not (Test-Path $script:AppPath)) {
            throw "SENTRY_QML_E2E_APP_PATH does not point to a wasm HTML file or directory: $script:AppPath"
        }
    } elseif ([string]::IsNullOrEmpty($script:AppPath) -or -not (Test-Path $script:AppPath)) {
        throw "SENTRY_QML_E2E_APP_PATH does not point to an executable: $script:AppPath"
    }

    New-Item -ItemType Directory -Path $script:OutputDir -Force | Out-Null
    if (-not $script:IsAndroid -and -not $script:IsWasmBrowser) {
        New-Item -ItemType Directory -Path $script:DatabasePath -Force | Out-Null
    }
    Set-OutputDir -Path $script:OutputDir

    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:SENTRY_QML_E2E_RUN_ID = $script:RunId
    $env:SENTRY_QML_E2E_DATABASE_PATH = $script:DatabasePath

    Connect-SentryApi `
        -ApiToken $env:SENTRY_AUTH_TOKEN `
        -Organization $script:SentryOrg `
        -Project $script:SentryProject `
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

    function script:Save-SentryTestEventAttachment {
        param(
            [Parameter(Mandatory = $true)]
            [string]$EventId,

            [Parameter(Mandatory = $true)]
            $Attachment,

            [Parameter(Mandatory = $true)]
            [string]$OutputPath
        )

        $attachmentId = Get-ObjectValue -InputObject $Attachment -Name 'id'
        if ([string]::IsNullOrEmpty([string]$attachmentId)) {
            throw 'Sentry event attachment does not have an id.'
        }

        $eventIdWithoutHyphens = $EventId -replace '-', ''
        $resource =
            "events/$eventIdWithoutHyphens/attachments/$attachmentId/?download=1"
        $uri = "$($script:BaseUrl)/api/0/projects/$($script:SentryOrgPath)/$($script:SentryProjectPath)/$resource"

        Invoke-WebRequest `
            -Uri $uri `
            -Method 'GET' `
            -Headers @{ Authorization = "Bearer $env:SENTRY_AUTH_TOKEN" } `
            -OutFile $OutputPath
    }

    function script:Get-E2EActionArguments {
        param(
            [Parameter(Mandatory = $true)]
            [string]$Action,

            [string[]]$AdditionalArgs = @()
        )

        return @(
            $Action,
            '--dsn',
            $script:AppDsn,
            '--run-id',
            $script:RunId,
            '--database-path',
            $script:DatabasePath
        ) + $AdditionalArgs
    }

    function script:Get-AndroidLaunchArguments {
        param(
            [Parameter(Mandatory = $true)]
            [string[]]$ApplicationArguments
        )

        $applicationArgumentsString = $ApplicationArguments -join ' '
        return @('-S', '--es', 'applicationArguments', "'''$applicationArgumentsString'''")
    }

    function script:Invoke-E2EAction {
        param(
            [Parameter(Mandatory = $true)]
            [string]$Action,

            [string[]]$AdditionalArgs = @()
        )

        $applicationArguments = Get-E2EActionArguments -Action $Action -AdditionalArgs $AdditionalArgs
        if ($script:IsWasmBrowser) {
            $runner = Join-Path $PSScriptRoot 'wasm/run-action.mjs'
            $nodeArguments = @($runner, $script:AppPath, '--') + $applicationArguments
            $output = & node @nodeArguments 2>&1
            $result = [pscustomobject]@{
                ExitCode = $LASTEXITCODE
                Output = @($output)
            }
            $result | ConvertTo-Json -Depth 8 | Out-File -FilePath (Get-OutputFilePath "$Action-result.json")
            return $result
        }

        $launchArguments = if ($script:IsAndroid) {
            Get-AndroidLaunchArguments -ApplicationArguments $applicationArguments
        } else {
            $applicationArguments
        }
        $result = Invoke-DeviceApp -ExecutablePath $script:AppPath -Arguments $launchArguments
        $result | ConvertTo-Json -Depth 8 | Out-File -FilePath (Get-OutputFilePath "$Action-result.json")
        return $result
    }

    function script:Get-AndroidAppPackageName {
        if (-not $script:IsAndroid) {
            throw 'Android app package name is only available for Android tests.'
        }

        return ($script:AppPath -split '/', 2)[0]
    }

    function script:Invoke-AndroidRunAs {
        param(
            [Parameter(Mandatory = $true)]
            [string[]]$Arguments
        )

        $packageName = Get-AndroidAppPackageName
        $output = & adb shell run-as $packageName @Arguments 2>&1
        if ($LASTEXITCODE -ne 0) {
            throw "adb run-as $packageName $($Arguments -join ' ') failed: $($output -join [Environment]::NewLine)"
        }

        return @($output)
    }

    function script:Get-AndroidNativeCrashEventId {
        $envelopePaths = Invoke-AndroidRunAs -Arguments @(
            'find',
            $script:DatabasePath,
            '-path',
            '*/.sentry-native/*.run/*.envelope',
            '-type',
            'f'
        )
        $envelopePath = $envelopePaths |
            ForEach-Object { "$_".Trim() } |
            Where-Object { $_ -match '\.envelope$' } |
            Sort-Object |
            Select-Object -Last 1
        if (-not $envelopePath) {
            throw "Could not find a native crash envelope in $script:DatabasePath."
        }

        $header = Invoke-AndroidRunAs -Arguments @('head', '-n', '1', $envelopePath)
        $eventId = (ConvertFrom-Json -InputObject ($header -join "`n")).event_id
        if ($eventId -notmatch '^[0-9a-f]{8}-?[0-9a-f]{4}-?[0-9a-f]{4}-?[0-9a-f]{4}-?[0-9a-f]{12}$') {
            throw "Native crash envelope does not contain a valid event_id: $eventId"
        }

        Write-Host "Found Android native crash event ID: $eventId" -ForegroundColor Green
        return $eventId
    }

    function script:Assert-CleanExit {
        param(
            [Parameter(Mandatory = $true)]
            $Result
        )

        if ($script:IsAndroid) {
            $Result.Output | Should -Not -BeNullOrEmpty
        } else {
            $Result.ExitCode | Should -Be 0
        }
    }

    function script:Assert-CrashExit {
        param(
            [Parameter(Mandatory = $true)]
            $Result
        )

        if ($script:IsAndroid) {
            $crashLogs = @($Result.Output | Where-Object { $_ -match 'Fatal signal \d+ \(SIG[^)]+\)' })
            $crashLogs | Should -Not -BeNullOrEmpty
        } else {
            $Result.ExitCode | Should -Not -Be 0
        }
    }

    function script:Skip-WasmCrashCapture {
        if ($script:IsWasmBrowser) {
            Set-ItResult -Skipped -Because 'Sentry JavaScript does not support the native crash flow used by this E2E app.'
            return $true
        }
        return $false
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
        $uri = "$($script:BaseUrl)/api/0/projects/$($script:SentryOrgPath)/$($script:SentryProjectPath)/issues/?$queryString"

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
        if (-not $script:IsWasmBrowser) {
            Connect-Device -Platform $script:DevicePlatform
            if ($script:IsAndroid) {
                Install-DeviceApp -Path $script:AppPackagePath | Out-Null
            }
        }
    }

    AfterAll {
        if (-not $script:IsWasmBrowser -and (Get-Command Disconnect-Device -ErrorAction SilentlyContinue)) {
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
            Assert-CleanExit -Result $script:MessageResult
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

    Context 'User consent' -Skip:$IsCocoa {
        BeforeAll {
            $script:ConsentMessage = "Sentry QML E2E consent $script:RunId"
            $script:ConsentResult = Invoke-E2EAction -Action 'consent-capture'
            $script:ConsentEventIds = Get-EventIds -AppOutput $script:ConsentResult.Output -ExpectedCount 1
            $script:ConsentEvent = Get-SentryTestEvent -EventId $script:ConsentEventIds[0] -TimeoutSeconds 180
        }

        It 'exits cleanly' {
            Assert-CleanExit -Result $script:ConsentResult
        }

        It 'captures the post-consent event in Sentry' {
            $script:ConsentEvent | Should -Not -BeNullOrEmpty
            Get-ObjectValue -InputObject $script:ConsentEvent -Name 'title' | Should -Be $script:ConsentMessage
        }

        It 'keeps the QML correlation tags' {
            Get-TagValue -SentryEvent $script:ConsentEvent -Key 'e2e_run_id' | Should -Be $script:RunId
            Get-TagValue -SentryEvent $script:ConsentEvent -Key 'test.action' | Should -Be 'consent-capture'
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
            Assert-CleanExit -Result $script:FeedbackResult
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

    Context 'View hierarchy capture' {
        BeforeAll {
            $script:ViewHierarchyMessage = "Sentry QML E2E view hierarchy $script:RunId"
            $script:ViewHierarchyResult = Invoke-E2EAction -Action 'view-hierarchy-capture'
            $script:ViewHierarchyEventIds = Get-EventIds -AppOutput $script:ViewHierarchyResult.Output -ExpectedCount 1
            $script:ViewHierarchyEvent = Get-SentryTestEvent `
                -EventId $script:ViewHierarchyEventIds[0] `
                -TimeoutSeconds 180
            $script:ViewHierarchyAttachments = Get-SentryTestEventAttachments `
                -EventId $script:ViewHierarchyEventIds[0] `
                -ExpectedCount 1 `
                -TimeoutSeconds 180
        }

        It 'exits cleanly' {
            Assert-CleanExit -Result $script:ViewHierarchyResult
        }

        It 'captures a message event in Sentry' {
            $script:ViewHierarchyEvent | Should -Not -BeNullOrEmpty
            Get-ObjectValue -InputObject $script:ViewHierarchyEvent -Name 'title' | Should -Be $script:ViewHierarchyMessage
            Get-TagValue -SentryEvent $script:ViewHierarchyEvent -Key 'e2e_run_id' | Should -Be $script:RunId
            Get-TagValue -SentryEvent $script:ViewHierarchyEvent -Key 'test.action' | Should -Be 'view-hierarchy-capture'
        }

        It 'uploads the view hierarchy attachment' {
            $viewHierarchyAttachment = @($script:ViewHierarchyAttachments) | Where-Object {
                (Get-ObjectValue -InputObject $_ -Name 'name') -eq 'view-hierarchy.json'
            } | Select-Object -First 1

            $viewHierarchyAttachment | Should -Not -BeNullOrEmpty
            Get-ObjectValue -InputObject $viewHierarchyAttachment -Name 'type' | Should -Be 'event.view_hierarchy'
            Get-ObjectValue -InputObject $viewHierarchyAttachment -Name 'mimetype' | Should -Be 'application/json'

            Save-SentryTestEventAttachment `
                -EventId $script:ViewHierarchyEventIds[0] `
                -Attachment $viewHierarchyAttachment `
                -OutputPath (Get-OutputFilePath 'view-hierarchy.json')
        }
    }

    Context 'Global attributes' {
        BeforeAll {
            $script:AttributesRunId = $script:RunId -replace '[^A-Za-z0-9_]', '_'
            $script:AttributesMessage = "Sentry QML E2E attributes $script:RunId"
            $script:AttributesResult = Invoke-E2EAction -Action 'attributes-capture'
            $script:AttributesLogs = Get-SentryTestLog `
                -AttributeName 'sentry_qml_e2e_run_id' `
                -AttributeValue $script:AttributesRunId `
                -TimeoutSeconds 180 `
                -Fields @('sentry_qml_e2e_local')
            $script:AttributesMetrics = Get-SentryTestMetric `
                -MetricName 'sentry_qml_e2e_attributes' `
                -AttributeName 'sentry_qml_e2e_run_id' `
                -AttributeValue $script:AttributesRunId `
                -TimeoutSeconds 180 `
                -Fields @('sentry_qml_e2e_local')
        }

        It 'exits cleanly' {
            Assert-CleanExit -Result $script:AttributesResult
        }

        It 'captures a log with the global attribute' {
            $script:AttributesLogs | Should -Not -BeNullOrEmpty
            $logsJson = $script:AttributesLogs | ConvertTo-Json -Depth 16 -Compress
            $logsJson.Contains($script:AttributesMessage) | Should -BeTrue
            $logsJson.Contains($script:AttributesRunId) | Should -BeTrue
            $logsJson.Contains('sentry_qml_e2e_local') | Should -BeTrue
        }

        It 'captures a metric with the global attribute' {
            $script:AttributesMetrics | Should -Not -BeNullOrEmpty
            $metricsJson = $script:AttributesMetrics | ConvertTo-Json -Depth 16 -Compress
            $metricsJson.Contains('sentry_qml_e2e_attributes') | Should -BeTrue
            $metricsJson.Contains($script:AttributesRunId) | Should -BeTrue
            $metricsJson.Contains('sentry_qml_e2e_local') | Should -BeTrue
        }
    }

    Context 'Crash capture' {
        BeforeAll {
            if ($script:IsWasmBrowser) {
                return
            }

            $script:CrashId = [guid]::NewGuid().ToString()
            $script:CrashResult = Invoke-E2EAction -Action 'crash-capture' -AdditionalArgs @('--crash-id', $script:CrashId)
            $script:CrashEventId = if ($script:IsAndroid) { Get-AndroidNativeCrashEventId } else { $null }
            $script:CrashSendResult = Invoke-E2EAction -Action 'crash-send'
            $script:CrashEvent = if ($script:IsAndroid) {
                Get-SentryTestEvent -EventId $script:CrashEventId -TimeoutSeconds 300
            } else {
                Get-SentryTestEvent -TagName 'test.crash_id' -TagValue $script:CrashId -TimeoutSeconds 300
            }
        }

        It 'crashes the app process' {
            if (Skip-WasmCrashCapture) {
                return
            }
            Assert-CrashExit -Result $script:CrashResult
        }

        It 'prints the crash correlation id' {
            if (Skip-WasmCrashCapture) {
                return
            }
            $script:CrashResult.Output |
                Where-Object { $_ -match [regex]::Escape("CRASH_ID: $script:CrashId") } |
                Should -Not -BeNullOrEmpty
        }

        It 'can relaunch to flush pending crash data' {
            if (Skip-WasmCrashCapture) {
                return
            }
            Assert-CleanExit -Result $script:CrashSendResult
        }

        It 'captures a crash event in Sentry' {
            if (Skip-WasmCrashCapture) {
                return
            }
            $script:CrashEvent | Should -Not -BeNullOrEmpty
            Get-ObjectValue -InputObject $script:CrashEvent -Name 'type' | Should -Be 'error'
        }

        It 'keeps the QML crash context' {
            if (Skip-WasmCrashCapture) {
                return
            }

            if ($script:IsAndroid) {
                Set-ItResult -Skipped -Because 'sentry-android NDK crash envelopes do not include QML scope tags.'
                return
            }

            Get-TagValue -SentryEvent $script:CrashEvent -Key 'e2e_run_id' | Should -Be $script:RunId
            Get-TagValue -SentryEvent $script:CrashEvent -Key 'test.action' | Should -Be 'crash-capture'
            Get-TagValue -SentryEvent $script:CrashEvent -Key 'test.crash_id' | Should -Be $script:CrashId
        }

        It 'contains exception information' {
            if (Skip-WasmCrashCapture) {
                return
            }

            $exception = Get-ObjectValue -InputObject $script:CrashEvent -Name 'exception'
            $exception | Should -Not -BeNullOrEmpty
            (Get-ObjectValue -InputObject $exception -Name 'values') | Should -Not -BeNullOrEmpty
        }
    }
}
