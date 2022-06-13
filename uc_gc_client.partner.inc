<?php

/**
 * @file
 * Functions for communicating with the GoCardless Client site's API.
 */

/**
 * Performs a GET HTTP request on the Partner site.
 *
 * Used in the payment method's settings form to test if the site is
 * 'connected' to the Partner.
 *
 * @return mixed
 *   An object if site is connected, and an array containing a single value,
 *   'FALSE' if the site is not connected.
 */
function uc_gc_client_get() {
  $data = array();
  $options['headers'] = array(
    'Content-Type' => 'application/json',
    'Cookie' => $_SESSION['uc_gc_client_cookie'],
    'X-CSRF-Token' => $_SESSION['uc_gc_client_token'],
  );
  $config = config('uc_gc_client.settings');
  $env = $config->get('sandbox') ? 'SANDBOX' : 'LIVE';
  $ext = $config->get('sandbox') ? '_sandbox' : '_live';
  $url = $config->get('partner_url') . '/gc/client/' . $config->get('org_id' . $ext) . '/' . $env;
  $response = backdrop_http_request($url, $options);
  $data = json_decode($response->data);
  return $data;
}

/**
 * Handles API posts to GC Partner site, and optionally logs results.
 *
 * @param array $params
 *   Array of data to be posted to the GoCardless API.
 *
 * @return mixed
 *   The API call result as an object, or a string containing the error code
 *   if the API call failed, or authentication failed.
 */
function uc_gc_client_api(array $params) {
  $config = config('uc_gc_client.settings');
  $auth = uc_gc_client_authenticate();
  if ($auth == 200) {
    $result = uc_gc_client_post($params);
    if (isset($result->error)) {
      $message = '<pre>' . t('Error code @code (@error)', array(
        '@code' => $result->code,
        '@error' => $result->error,
      )) . '<br />' . print_r($result, TRUE) . '</pre>';
      backdrop_set_message($message, 'error');
      if ($config->get('debug_api')) {
        watchdog("uc_gc_client", $message, NULL, WATCHDOG_ERROR);
      }
      return $message;
    }
    else {
      if ($config->get('debug_api')) {
        $message = '<pre>' . t('GoCardless API response:') . '<br />' . print_r($result, TRUE) . '</pre>';
        watchdog("uc_gc_client", $message);
      }
      return $result;
    }
  }
  else {
    backdrop_set_message(t('Error @code connecting with partner site', array(
      '@code' => $auth,
    )), 'error');
    if ($config->get('debug_api')) {
      $message = '<pre>' . print_r($auth, TRUE) . '</pre>';
      watchdog("uc_gc_client", $message, NULL, WATCHDOG_ERROR);
    }
    return $auth;
  }
}

/**
 * Handles Sevices module session authentication with GC Partner site.
 *
 * @return int
 *   200 if already logged in to Partner site, or else the resulting code from
 *   authentication attempt.
 */
function uc_gc_client_authenticate() {
  //unset($_SESSION['uc_gc_client_cookie_created']);
  if (isset($_SESSION['uc_gc_client_cookie_created']) && $_SESSION['uc_gc_client_cookie_created'] < REQUEST_TIME - 1800) {
    unset($_SESSION['uc_gc_client_cookie']);
    unset($_SESSION['uc_gc_client_cookie_created']);
  }

  if (!isset($_SESSION['uc_gc_client_cookie_created'])) {
    $config = config('uc_gc_client.settings');
    $ext = $config->get('sandbox') ? '_sandbox' : '_live';
    // Login first.
    $data = array(
      'username' => $config->get('user_name' . $ext),
      'password' => $config->get('user_pass' . $ext),
    );
    if ($data['username'] && $data['password']) {
      $data = backdrop_json_encode($data);
      $url = $config->get('partner_url') . '/gc_connect/user/login';
      $options = array(
        'headers' => array(
          'Content-Type' => 'application/json',
        ),
        'method' => 'POST',
        'data' => $data,
      );
      $result = backdrop_http_request($url, $options);
      
      $result_data = json_decode($result->data);

      if ($result->code == 200) {
        // Get X-CRSF token, and save cookie and token.
        $_SESSION['uc_gc_client_cookie'] = $result_data->session_name . '=' . $result_data->sessid;
        $_SESSION['uc_gc_client_cookie_created'] = REQUEST_TIME;
        $xcrf_url = $config->get('partner_url') . '/services/session/token';
        $xcrf_options = array(
          'method' => 'GET',
          'headers' => array(
            'Cookie' => $_SESSION['uc_gc_client_cookie'],
          ),
        );
        $xcrf_result = backdrop_http_request($xcrf_url, $xcrf_options);
        $token = $xcrf_result->data;
        $_SESSION['uc_gc_client_token'] = $token;
      }
      return $result->code;
    }
    else {
      return 'User name and password not set';
    }
  }
  else {
    // Already logged in.
    return 200;
  }
}

/**
 * Handles HTTP requests to GC Partner site.
 *
 * @param array $data
 *   The parameters to post to the GoCardless API.
 *
 * @return object
 *   The data element of the response if the API call was successful, or the
 *   entire response if the call failed.
 */
function uc_gc_client_post(array $data) {
  $config = config('uc_gc_client.settings');
  $options['headers'] = array(
    'Content-Type' => 'application/json',
    'Cookie' => $_SESSION['uc_gc_client_cookie'],
    'X-CSRF-Token' => $_SESSION['uc_gc_client_token'],
  );
  $data['environment'] = $config->get('sandbox') ? 'SANDBOX' : 'LIVE';
  $ext = $config->get('sandbox') ? '_sandbox' : '_live';
  $data = backdrop_json_encode($data);
  $options['data'] = $data;
  $options['method'] = 'POST';
  $url = $config->get('partner_url') . '/gc/client/' . $config->get('org_id' . $ext);
  $response = backdrop_http_request($url, $options);
  if (isset($response->error)) {
    return $response;
  }
  else {
    $data = json_decode($response->data);
    return $data;
  }
}

/**
 * Callback function: Saves key variables for connecting with Partner site.
 *
 * Variables are posted here from Partner site, following completion of
 * GoCardless OAuth flow.
 */
function uc_gc_client_connect() {
  $config = config('uc_gc_client.settings');
  if (isset($_POST['environ'])) {
    $ext = $_POST['environ'] == 'SANDBOX' ? '_sandbox' : '_live';
  }

  // Check that the returned password matches the saved one before
  // proceeding, and return error 403 Forbidden if it does not.
  if (isset($ext) && isset($_POST['pass']) && $_POST['pass'] == $config->get('user_pass' . $ext) && isset($_POST['id']) && isset($_POST['name'])) {
    $config->set('org_id' . $ext, $_POST['id']);
    $config->set('user_name' . $ext, $_POST['name']);
    $config->set('user_pass' . $ext, $_POST['pass']);
    $config->save();
  }
  else {
    backdrop_add_http_header('Status', '403 There was a problem connecting.');
  }

  // Unset to reduce likelihood of authentication failure if client id
  // reconnecting.
  if (isset($_SESSION['uc_gc_client_cookie_created'])) {
    unset($_SESSION['uc_gc_client_cookie_created']);
  }
}

/**
 * Implements hook_form_submit().
 *
 * Disconnects client site from GC partner site.
 */
function uc_gc_client_disconnect_submit($form, $form_state) {
  $config = config('uc_gc_client.settings');
  $auth = uc_gc_client_authenticate();
  if ($auth == 200) {
    $data = array(
      'endpoint' => 'oauth',
      'action' => 'revoke',
    );
    $result = uc_gc_client_post($data);
    if ($result->response == 200) {
      backdrop_set_message(t('You have disconnected successfully from GoCardless'));
    }
    else {
      backdrop_set_message(t('There was a problem disconnecting from GoCardless'), 'error');
    }
    if (isset($_SESSION['uc_gc_client_cookie_created'])) {
      unset($_SESSION['uc_gc_client_cookie_created']);
    }
    $ext = $form_state['values']['ext'];
    $config->set('org_id' . $ext, '');
    $config->set('user_name' . $ext, '');
    $config->set('user_pass' . $ext, '');
    $config->save();
  }
}
